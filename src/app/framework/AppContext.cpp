#include "app/framework/AppContext.h"

namespace suspkin {

// Out of line so the vtable has one home rather than one per translation unit.
AppContext::~AppContext() = default;

} // namespace suspkin
