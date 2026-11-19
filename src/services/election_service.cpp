#include "election_service.h"
#include <cctype>
#include <algorithm>

namespace {
bool validDate(const std::string& value) {
    if (value.size()!=10 || value[4]!='-' || value[7]!='-') return false;
    for (size_t i=0;i<value.size();++i) if(i!=4&&i!=7&&!std::isdigit((unsigned char)value[i])) return false;
    int y=std::stoi(value.substr(0,4)), m=std::stoi(value.substr(5,2)), d=std::stoi(value.substr(8,2));
    if(m<1||m>12) return false;
    static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int max=days[m-1]; if(m==2&&(y%400==0||(y%4==0&&y%100!=0))) max=29;
    return d>=1&&d<=max;
}
bool validTimestamp(const std::string& s) {
    if(s.empty()) return true;
    if(s.size()!=20||s[10]!='T'||s[13]!=':'||s[16]!=':'||s[19]!='Z'||!validDate(s.substr(0,10))) return false;
    for(size_t i=11;i<19;++i) if(i!=13&&i!=16&&!std::isdigit((unsigned char)s[i])) return false;
    return std::stoi(s.substr(11,2))<=23&&std::stoi(s.substr(14,2))<=59&&std::stoi(s.substr(17,2))<=59;
}
bool validConfiguration(const Election& e) {
    return !e.name.empty()&&e.name.size()<=200&&e.description.size()<=2000&&
        validTimestamp(e.startAt)&&validTimestamp(e.endAt)&&
        (e.startAt.empty()||e.endAt.empty()||e.endAt>e.startAt);
}
bool canManage(const User& user, const Election& e) {
    return user.role=="platform_admin"||(user.role=="org_admin"&&user.orgId>0&&user.orgId==e.orgId);
}
}

std::vector<Election> ElectionService::list(const User& actor) { return repository_.listVisible(actor); }
ElectionService::Result ElectionService::get(const User& actor,long long id) {
    if(id<=0) return {Status::Invalid,{}};
    auto e=repository_.get(id); if(!e) return {Status::NotFound,{}};
    if(!canManage(actor,*e)) {
        const auto visible=repository_.listVisible(actor);
        const bool allowed=std::any_of(visible.begin(),visible.end(),[id](const Election& item){return item.id==id;});
        if(!allowed) return {Status::NotFound,{}};
    }
    return {Status::Ok,*e};
}
ElectionService::Result ElectionService::create(const User& actor,Election e) {
    if(actor.role!="platform_admin"&&!(actor.role=="org_admin"&&actor.orgId==e.orgId&&e.orgId>0)) return {Status::Forbidden,{}};
    if(e.orgId<=0||!validConfiguration(e)) return {Status::Invalid,{}};
    e.status="draft";
    if(repository_.create(e)!=Store::CreateElectionResult::Ok) return {Status::NotFound,{}};
    return {Status::Ok,e};
}
ElectionService::Result ElectionService::updateDraft(const User& actor,long long id,const Election& changes) {
    if(id<=0) return {Status::Invalid,{}};
    auto current=repository_.get(id); if(!current) return {Status::NotFound,{}};
    if(!canManage(actor,*current)) return {Status::Forbidden,{}};
    if(current->status!="draft") return {Status::Conflict,*current};
    Election next=*current;
    next.name=changes.name; next.description=changes.description; next.startAt=changes.startAt; next.endAt=changes.endAt;
    next.discoverable=changes.discoverable; next.showLiveResults=changes.showLiveResults;
    if(!validConfiguration(next)) return {Status::Invalid,{}};
    if(!repository_.updateDraft(id,next)) return {Status::Conflict,{}};
    return {Status::Ok,*repository_.get(id)};
}
