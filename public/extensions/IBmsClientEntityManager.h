#ifndef _INCLUDE_IBMS_CLIENT_ENTITY_MANAGER_H_
#define _INCLUDE_IBMS_CLIENT_ENTITY_MANAGER_H_

#include <IShareSys.h>

#define SMINTERFACE_BMS_CLIENT_ENTITY_MANAGER_NAME "IBmsClientEntityManager"
#define SMINTERFACE_BMS_CLIENT_ENTITY_MANAGER_VERSION 5

// ResolveClientEntityRef accepts a networked entindex, a client-only raw
// CBaseHandle::ToInt(), or a proper high-bit-marked client handle containing
// both the entry index and serial.

namespace SourceMod
{
class IBmsClientEntityManagerListener
{
public:
  virtual ~IBmsClientEntityManagerListener() = default;
  virtual void OnClientEntityCreated(void *entity, int clientRef, const char *classname) {}
  virtual void OnClientEntityDestroyed(void *entity, int clientRef) {}
};

class IBmsClientEntityManager : public SMInterface
{
public:
  const char *GetInterfaceName() override
  {
    return SMINTERFACE_BMS_CLIENT_ENTITY_MANAGER_NAME;
  }

  unsigned int GetInterfaceVersion() override
  {
    return SMINTERFACE_BMS_CLIENT_ENTITY_MANAGER_VERSION;
  }

public:
  virtual void *ResolveClientEntityRef(int clientRef) = 0;
  virtual int EntityToClientRef(void *entity) = 0;
  virtual int EntityToClientHandleRef(void *entity) = 0;
  virtual bool IsSameClientEntity(void *entity, int clientHandleRef) = 0;
  virtual int ClientHandleToEntityRef(const void *handleAddress) = 0;
  virtual bool EntityRefToClientHandle(int clientRef, void *handleAddress) = 0;
  virtual int GetClientEntityCount() const = 0;
  virtual int GetEntityRefByOrdinalClient(int ordinal) const = 0;
  virtual const char *GetEntityClassnameClient(int clientRef) = 0;
  virtual void AddClientEntityListener(IBmsClientEntityManagerListener *listener) = 0;
  virtual void RemoveClientEntityListener(IBmsClientEntityManagerListener *listener) = 0;
};
}

#endif
