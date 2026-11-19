#ifndef ORGANIZATION_REPOSITORY_H
#define ORGANIZATION_REPOSITORY_H
#include "../store.h"
class OrganizationRepository {
public:
    explicit OrganizationRepository(Store& store) : store_(store) {}
    std::optional<Organization> get(long long id) { return store_.getOrganization(id); }
    std::vector<Organization> list() { return store_.listOrganizations(); }
    std::optional<Organization> bySlug(const std::string& slug) { return store_.getOrganizationBySlug(slug); }
    Organization create(const std::string& name, const std::string& slug, bool selfReg) {
        return store_.createOrganization(name, slug, selfReg);
    }
    void update(long long id, const std::string& name, const std::string& slug, bool selfReg) {
        store_.updateOrganization(id, name, slug, selfReg);
    }
private:
    Store& store_;
};
#endif
