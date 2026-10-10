#pragma once

namespace join {
// Lets the engine open its own two-player loopback connection when an owned
// offline session starts a map. Required: nothing else joins player two.
bool Install();
// Call only after the shared RemoveHooks operation.
void Shutdown();
bool Active();
}
