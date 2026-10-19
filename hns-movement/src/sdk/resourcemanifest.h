#pragma once

// IEntityResourceManifest is only forward declared in the hl2sdk-cs2 revision this plugin pins. Upstream hl2sdk
// (public/entity2/entitysystem.h, also what CS2KZ builds against) defines it as below. Only the head of the vtable is
// declared: the three AddResource overloads in the same order as upstream, so every compiler lays them out the same way
// (MSVC groups overloads and reverses them, which is why all three are declared even though only the first is used).
class CEntityInstance;

class IEntityResourceManifest
{
public:
	virtual void AddResource(const char *pszResourceName) = 0;
	virtual void AddResource(const char *pszResourceName, void *pfnCallback) = 0;
	virtual void AddResource(const char *pszResourceName, CEntityInstance *pEntity, void *callback) = 0;
};
