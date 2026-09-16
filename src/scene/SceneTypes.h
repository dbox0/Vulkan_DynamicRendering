#pragma once

// Registers the reflected scene types (Node, LightType) with reflect::.
//
// Called exactly once at startup, before anything serializes or snapshots a
// node. Declared at global scope because Node befriends it to reach the
// private transform fields.
void registerSceneTypes();
