#include "app/framework/FeatureRegistry.h"

namespace suspkin {

Feature::~Feature() = default;

namespace {

/// A function-local static rather than a file-level one: features register from
/// their own static initialisers, and a file-level vector might not have been
/// constructed yet when the first of them runs.
std::vector<FeatureRegistry::Factory>& factories()
{
    static std::vector<FeatureRegistry::Factory> registered;
    return registered;
}

} // namespace

void FeatureRegistry::add(Factory factory)
{
    factories().push_back(std::move(factory));
}

std::vector<std::unique_ptr<Feature>> FeatureRegistry::createAll(AppContext& context)
{
    std::vector<std::unique_ptr<Feature>> features;
    features.reserve(factories().size());
    for (const Factory& factory : factories()) features.push_back(factory(context));
    return features;
}

} // namespace suspkin
