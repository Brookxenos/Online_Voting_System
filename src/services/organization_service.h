#ifndef ORGANIZATION_SERVICE_H
#define ORGANIZATION_SERVICE_H
#include "../repositories/organization_repository.h"
class OrganizationService {
public:
    enum class Status { Ok, Forbidden, Invalid, NotFound, Duplicate };
    struct Result { Status status = Status::Invalid; Organization organization; };
    explicit OrganizationService(OrganizationRepository& repository) : repository_(repository) {}
    std::vector<Organization> list(const User& actor, Status& status);
    Result get(const User& actor, long long id);
    Result create(const User& actor, const std::string& name, const std::string& slug, bool selfReg);
    Result update(const User& actor, long long id, const std::string& name,
                  const std::string& slug, bool selfReg);
private:
    OrganizationRepository& repository_;
};
#endif
