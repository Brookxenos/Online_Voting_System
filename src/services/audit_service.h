#ifndef ELECTRAVOTE_AUDIT_SERVICE_H
#define ELECTRAVOTE_AUDIT_SERVICE_H
#include "../store.h"
class AuditService {
public:
    explicit AuditService(Store& store) : store_(store) {}
    void lifecycleRejected(const std::string& actor, long long electionId, const std::string& requested) {
        store_.addAudit(actor, "LIFECYCLE_REJECTED", electionId, "requested status " + requested);
    }
private:
    Store& store_;
};
#endif
