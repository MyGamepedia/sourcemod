#include "client_entities.h"

#include "extension.h"
#include "listeners.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <bridge/include/IVEngineServerBridge.h>

#if defined(_WIN32)
# include <Windows.h>
#endif

namespace
{
constexpr uint32_t kClientEntRefMask = uint32_t{1} << 31;
constexpr int kMaxNetworkedEntities = 2048;
constexpr int kMaxEngineListeners = 1024;
constexpr int kMaxEngineListenerAllocation = 4096;

void SetError(char *error, size_t maxlength, const char *format, ...)
{
	if (!error || !maxlength)
		return;
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(error, maxlength, format, arguments);
	va_end(arguments);
	error[maxlength - 1] = '\0';
}
}

DHooksClientEntityManager g_DHooksClientEntities;

bool DHooksClientEntityManager::Initialize(SourceMod::IGameConfig *gameConfig,
	char *error, size_t maxlength)
{
	Shutdown();
	if (error && maxlength)
		error[0] = '\0';

#if !defined(_WIN32) || defined(_WIN64)
	return false;
#else
	
	if (engine->IsDedicatedServer())
		return false;

	if (!gameConfig)
	{
		SetError(error, maxlength, "Gamedata gamedata/dhooks.games.txt is unavailable!");
		return false;
	}
	if (!ResolveClientInterfaces(error, maxlength) || !RegisterEngineListener(gameConfig, error, maxlength))
	{
		Shutdown();
		return false;
	}

	available_ = true;
	SeedExistingEntities();
	
	if (smutils)
	{
		smutils->LogMessage(myself,
			"DHooks client entity support initialized: %u existing entities",
			static_cast<unsigned int>(entities_.size()));
	}
	
	return true;
#endif
}

bool DHooksClientEntityManager::ResolveClientInterfaces(char *error, size_t maxlength)
{
#if defined(_WIN32) && !defined(_WIN64)
	HMODULE clientModule = GetModuleHandleA("client.dll");
	if (!clientModule)
	{
		SetError(error, maxlength,
			"client.dll is not loaded (expected on a dedicated server)");
		return false;
	}

	using CreateInterfaceFn = void *(__cdecl *)(const char *, int *);
	auto factory = reinterpret_cast<CreateInterfaceFn>(
		GetProcAddress(clientModule, "CreateInterface"));
	if (!factory)
	{
		SetError(error, maxlength, "client.dll does not export CreateInterface");
		return false;
	}

	clientEntityList_ = static_cast<IDHooksClientEntityList *>(
		factory("VClientEntityList003", nullptr));
	clientTools_ = static_cast<IDHooksClientTools *>(
		factory("VCLIENTTOOLS001", nullptr));
	if (!clientEntityList_)
	{
		SetError(error, maxlength, "client.dll does not expose VClientEntityList003");
		return false;
	}
	if (!clientTools_)
	{
		SetError(error, maxlength, "client.dll does not expose VCLIENTTOOLS001");
		return false;
	}

	HMODULE tier0Module = GetModuleHandleA("tier0.dll");
	if (!tier0Module)
	{
		SetError(error, maxlength, "tier0.dll is not loaded");
		return false;
	}
	void *allocatorExport = GetProcAddress(tier0Module, "g_pMemAlloc");
	if (!allocatorExport)
	{
		SetError(error, maxlength, "tier0.dll does not export g_pMemAlloc");
		return false;
	}
	memoryAllocator_ = *reinterpret_cast<IDHooksMemAlloc **>(allocatorExport);
	if (!memoryAllocator_)
	{
		SetError(error, maxlength, "tier0 g_pMemAlloc is null");
		return false;
	}
	return true;
#else
	(void)error;
	(void)maxlength;
	return false;
#endif
}

bool DHooksClientEntityManager::RegisterEngineListener(
	SourceMod::IGameConfig *gameConfig, char *error, size_t maxlength)
{
	int listenersOffset = -1;
	if (!gameConfig->GetOffset("ClientEntityListeners", &listenersOffset))
	{
		SetError(error, maxlength,
			"missing ClientEntityListeners offset in dhooks gamedata");
		return false;
	}
	if (listenersOffset <= 0 || listenersOffset > 0x1000)
	{
		SetError(error, maxlength,
			"implausible ClientEntityListeners offset 0x%X", listenersOffset);
		return false;
	}

	engineListeners_ = reinterpret_cast<DHooksClientListenerVector *>(
		reinterpret_cast<uint8_t *>(clientEntityList_) + listenersOffset);
	const int count = engineListeners_->Count();
	const int allocated = engineListeners_->AllocationCount();
	if (count < 0 || count > kMaxEngineListeners || allocated < count ||
		allocated > kMaxEngineListenerAllocation ||
		(allocated > 0 && !engineListeners_->Base()) ||
		!engineListeners_->HasMatchingDebugPointer())
	{
		SetError(error, maxlength,
			"ClientEntityListeners offset 0x%X produced invalid vector metadata",
			listenersOffset);
		engineListeners_ = nullptr;
		return false;
	}

	if (engineListeners_->Find(this) < 0 &&
		!engineListeners_->AddToTail(this, memoryAllocator_))
	{
		SetError(error, maxlength,
			"could not append DHooks to the client entity listener vector");
		engineListeners_ = nullptr;
		return false;
	}
	engineListenerRegistered_ = true;
	return true;
}

void DHooksClientEntityManager::UnregisterEngineListener()
{
	if (engineListenerRegistered_ && engineListeners_)
		engineListeners_->FindAndRemove(this);
	engineListenerRegistered_ = false;
	engineListeners_ = nullptr;
}

void DHooksClientEntityManager::SeedExistingEntities()
{
	if (!clientTools_)
		return;
	for (void *entity = clientTools_->FirstEntity(); entity;
		entity = clientTools_->NextEntity(entity))
	{
		TrackEntity(entity, false);
	}
}

void DHooksClientEntityManager::TrackEntity(void *entity, bool notify)
{
	if (!available_ || !entity || entities_.find(entity) != entities_.end())
		return;

	auto *unknown = reinterpret_cast<IDHooksClientUnknown *>(entity);
	const CBaseHandle &handle = unknown->GetRefEHandle();
	if (!handle.IsValid())
		return;

	IDHooksClientNetworkable *networkable = unknown->GetClientNetworkable();
	const int entIndex = networkable ? networkable->entindex() : -1;
	ClientEntityRecord record;
	record.clientRef = entIndex >= 0 ? entIndex : handle.ToInt();
	record.handleValue = static_cast<uint32_t>(handle.ToInt());
	record.classname = ReadEffectiveClassname(entity, entIndex);
	if (record.classname.empty())
		record.classname = "C_BaseEntity";

	const int clientRef = record.clientRef;
	const std::string classname = record.classname;
	entities_.emplace(entity, std::move(record));
	if (notify && g_pEntityListener)
		g_pEntityListener->OnClientEntityCreated(entity, clientRef, classname.c_str());
}

std::string DHooksClientEntityManager::ReadClientClassname(void *entity) const
{
	if (!entity || !clientTools_)
		return {};

	const int recordablesBefore = clientTools_->GetNumRecordables();
	const DHooksToolHandle handle = clientTools_->AttachToEntity(entity);
	const bool attachedByUs = handle != DHOOKS_TOOL_HANDLE_INVALID &&
		clientTools_->GetNumRecordables() == recordablesBefore + 1;

	std::string classname;
	if (handle != DHOOKS_TOOL_HANDLE_INVALID && clientTools_->IsValidHandle(handle))
	{
		const char *raw = clientTools_->GetClassname(handle);
		if (raw && raw[0])
			classname.assign(raw);
	}
	if (attachedByUs)
		clientTools_->DetachFromEntity(entity);

	constexpr char classPrefix[] = "class ";
	constexpr char structPrefix[] = "struct ";
	if (classname.compare(0, sizeof(classPrefix) - 1, classPrefix) == 0)
		classname.erase(0, sizeof(classPrefix) - 1);
	else if (classname.compare(0, sizeof(structPrefix) - 1, structPrefix) == 0)
		classname.erase(0, sizeof(structPrefix) - 1);
	return classname;
}

std::string DHooksClientEntityManager::ReadEffectiveClassname(
	void *entity, int entIndex) const
{
	if (entIndex >= 0 && gamehelpers)
	{
		CBaseEntity *serverEntity = gamehelpers->ReferenceToEntity(entIndex);
		if (serverEntity)
		{
			const char *classname = gamehelpers->GetEntityClassname(serverEntity);
			if (classname && classname[0])
				return classname;
		}
	}
	return ReadClientClassname(entity);
}

void DHooksClientEntityManager::OnEntityCreated(void *entity)
{
	TrackEntity(entity, true);
}

void DHooksClientEntityManager::OnEntityDeleted(void *entity)
{
	auto found = entities_.find(entity);
	if (!available_ || found == entities_.end() || found->second.deleting)
		return;
	found->second.deleting = true;
	const int clientRef = found->second.clientRef;
	const uint32_t handleValue = found->second.handleValue;
	if (g_pEntityListener)
		g_pEntityListener->OnClientEntityDestroyed(entity, clientRef);

	// SourcePawn callbacks may create entities and rehash the record map.
	found = entities_.find(entity);
	if (found != entities_.end() && found->second.handleValue == handleValue)
		entities_.erase(found);
}

void *DHooksClientEntityManager::ResolveClientEntityRef(int clientRef)
{
	const uint32_t encoded = static_cast<uint32_t>(clientRef);
	if (!available_ || !clientEntityList_ || encoded == INVALID_EHANDLE_INDEX)
		return nullptr;

	const bool properRef = (encoded & kClientEntRefMask) != 0;
	if (!properRef && clientRef >= 0 && clientRef < kMaxNetworkedEntities)
	{
		void *clientEntity = clientEntityList_->GetClientEntity(clientRef);
		if (!clientEntity)
			return nullptr;
		auto *unknown = reinterpret_cast<IDHooksClientUnknown *>(clientEntity);
		void *entity = unknown->GetBaseEntity();
		const auto found = entities_.find(entity);
		if (found == entities_.end() || found->second.clientRef != clientRef ||
			found->second.handleValue !=
				static_cast<uint32_t>(unknown->GetRefEHandle().ToInt()))
		{
			return nullptr;
		}
		return entity;
	}

	const uint32_t handleValue = properRef ? encoded & ~kClientEntRefMask : encoded;
	const CBaseHandle handle(handleValue);
	IDHooksClientUnknown *unknown = clientEntityList_->GetClientUnknownFromHandle(handle);
	if (!unknown || unknown->GetRefEHandle() != handle)
		return nullptr;
	void *entity = unknown->GetBaseEntity();
	const auto found = entities_.find(entity);
	if (found == entities_.end() || found->second.handleValue != handleValue)
		return nullptr;
	return entity;
}

int DHooksClientEntityManager::EntityToClientRef(void *entity) const
{
	const auto found = entities_.find(entity);
	return found == entities_.end() ? -1 : found->second.clientRef;
}

int DHooksClientEntityManager::EntityToClientHandleRef(void *entity) const
{
	const auto found = entities_.find(entity);
	return found == entities_.end()
		? -1
		: static_cast<int>(found->second.handleValue | kClientEntRefMask);
}

bool DHooksClientEntityManager::IsSameClientEntity(
	void *entity, int clientHandleRef) const
{
	const auto found = entities_.find(entity);
	if (!available_ || found == entities_.end())
		return false;
	const uint32_t encoded = static_cast<uint32_t>(clientHandleRef);
	if ((encoded & kClientEntRefMask) == 0 || encoded == INVALID_EHANDLE_INDEX)
		return false;
	const uint32_t expectedHandle = encoded & ~kClientEntRefMask;
	if (found->second.handleValue != expectedHandle)
		return false;
	const CBaseHandle &current =
		reinterpret_cast<IDHooksClientUnknown *>(entity)->GetRefEHandle();
	return current.IsValid() &&
		static_cast<uint32_t>(current.ToInt()) == expectedHandle;
}

int DHooksClientEntityManager::ClientHandleToEntityRef(const void *address) const
{
	if (!available_ || !address || !clientEntityList_)
		return -1;
	const CBaseHandle &handle = *static_cast<const CBaseHandle *>(address);
	if (!handle.IsValid())
		return -1;
	IDHooksClientUnknown *unknown = clientEntityList_->GetClientUnknownFromHandle(handle);
	if (!unknown || unknown->GetRefEHandle() != handle)
		return -1;
	void *entity = unknown->GetBaseEntity();
	const auto found = entities_.find(entity);
	return found == entities_.end() ||
		found->second.handleValue != static_cast<uint32_t>(handle.ToInt())
		? -1 : found->second.clientRef;
}

bool DHooksClientEntityManager::EntityRefToClientHandle(int clientRef, void *address)
{
	if (!available_ || !address)
		return false;
	auto *destination = static_cast<CBaseHandle *>(address);
	if (clientRef == -1)
	{
		destination->Term();
		return true;
	}
	void *entity = ResolveClientEntityRef(clientRef);
	if (!entity)
		return false;
	const CBaseHandle &source =
		reinterpret_cast<IDHooksClientUnknown *>(entity)->GetRefEHandle();
	if (!source.IsValid())
		return false;
	*destination = source;
	return true;
}

void DHooksClientEntityManager::Shutdown()
{
	available_ = false;
	UnregisterEngineListener();
	entities_.clear();
	clientTools_ = nullptr;
	clientEntityList_ = nullptr;
	memoryAllocator_ = nullptr;
}

#if defined(DHOOKS_CLIENT_ENTITIES_TESTS)
void DHooksClientEntityManager::InitializeForTests(
	IDHooksClientEntityList *entityList, IDHooksClientTools *clientTools)
{
	Shutdown();
	clientEntityList_ = entityList;
	clientTools_ = clientTools;
	available_ = entityList != nullptr;
	SeedExistingEntities();
}
#endif
