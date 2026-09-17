#pragma once

#include "app/framework/Feature.h"

#include <functional>
#include <memory>
#include <vector>

namespace suspkin {

class AppContext;

/// Every feature the application is built from.
///
/// A feature registers itself from its own translation unit, so the window has
/// no list of them to keep and adding one touches no existing file. The order
/// they are created in is the link order and so unspecified -- nothing may
/// depend on it, which is why where a command appears is decided by
/// RibbonSlot::order rather than by who registered first.
class FeatureRegistry {
public:
    using Factory = std::function<std::unique_ptr<Feature>(AppContext&)>;

    /// Called from a static initialiser, before main(). Do not call it by hand.
    static void add(Factory factory);

    /// One of each, in registration order.
    static std::vector<std::unique_ptr<Feature>> createAll(AppContext& context);
};

/// The static object that does the registering. Used through SUSPKIN_FEATURE.
template <class T>
struct FeatureRegistrar {
    FeatureRegistrar()
    {
        FeatureRegistry::add([](AppContext& context) -> std::unique_ptr<Feature> {
            return std::make_unique<T>(context);
        });
    }
};

/// Put this at the bottom of a feature's .cpp and the application has it.
///
/// It works because features are compiled into the executable rather than into
/// a static library: a static initialiser in an archive member nothing refers
/// to is dropped by the linker. If these ever move into suspkin_core, they will
/// need --whole-archive or a call that names them.
#define SUSPKIN_FEATURE(Type) \
    namespace { const ::suspkin::FeatureRegistrar<Type> s_##Type##Registrar; }

} // namespace suspkin
