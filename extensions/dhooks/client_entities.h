#ifndef _INCLUDE_DHOOKS_CLIENT_ENTITIES_H_
#define _INCLUDE_DHOOKS_CLIENT_ENTITIES_H_

#include "client_entity_hacks.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace SourceMod { class IGameConfig; }

class DHooksClientEntityManager final : public IDHooksClientEntityListener
{
public:
	bool Initialize(SourceMod::IGameConfig *gameConfig, char *error, size_t maxlength);
	void Shutdown();
	bool IsAvailable() const { return available_; }

	void *ResolveClientEntityRef(int clientRef);
	int EntityToClientRef(void *entity) const;
	int EntityToClientHandleRef(void *entity) const;
	bool IsSameClientEntity(void *entity, int clientHandleRef) const;
	int ClientHandleToEntityRef(const void *handleAddress) const;
	bool EntityRefToClientHandle(int clientRef, void *handleAddress);

	void OnEntityCreated(void *entity) override;
	void OnEntityDeleted(void *entity) override;

#if defined(DHOOKS_CLIENT_ENTITIES_TESTS)
	void InitializeForTests(IDHooksClientEntityList *entityList,
		IDHooksClientTools *clientTools = nullptr);
	size_t TrackedCountForTests() const { return entities_.size(); }
#endif

private:
	struct ClientEntityRecord
	{
		int clientRef;
		uint32_t handleValue;
		bool deleting = false;
		std::string classname;
	};

	bool ResolveClientInterfaces(char *error, size_t maxlength);
	bool RegisterEngineListener(SourceMod::IGameConfig *gameConfig,
		char *error, size_t maxlength);
	void UnregisterEngineListener();
	void SeedExistingEntities();
	void TrackEntity(void *entity, bool notify);
	std::string ReadClientClassname(void *entity) const;
	std::string ReadEffectiveClassname(void *entity, int entIndex) const;

private:
	bool available_ = false;
	IDHooksClientEntityList *clientEntityList_ = nullptr;
	IDHooksClientTools *clientTools_ = nullptr;
	DHooksClientListenerVector *engineListeners_ = nullptr;
	IDHooksMemAlloc *memoryAllocator_ = nullptr;
	bool engineListenerRegistered_ = false;
	std::unordered_map<void *, ClientEntityRecord> entities_;
};

extern DHooksClientEntityManager g_DHooksClientEntities;

#endif
