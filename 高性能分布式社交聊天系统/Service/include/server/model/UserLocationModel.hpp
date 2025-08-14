#ifndef USERLOCATIONMODEL_H
#define USERLOCATIONMODEL_H

#include <string>
#include <vector>
#include <cmath>
#include "User.hpp"

using namespace std;

// 地理位置信息结构
struct LocationInfo
{
    int userid;
    double latitude;
    double longitude;
    string last_update;
    bool is_visible;
    string location_name;
    
    LocationInfo(int id = 0, double lat = 0.0, double lng = 0.0, 
                 const string& update = "", bool visible = true, 
                 const string& name = "")
        : userid(id), latitude(lat), longitude(lng), 
          last_update(update), is_visible(visible), location_name(name) {}
};

// 附近用户信息结构 (包含距离信息)
struct NearbyUser
{
    User user;
    LocationInfo location;
    double distance; // 距离(米)
    
    NearbyUser(const User& u, const LocationInfo& loc, double dist)
        : user(u), location(loc), distance(dist) {}
};

// 维护用户位置信息的数据访问类
class UserLocationModel
{
public:
    // 更新用户位置信息
    bool update_user_location(int userid, double latitude, double longitude, 
                              bool is_visible = true, const string& location_name = "");

    // 获取用户当前位置
    LocationInfo get_user_location(int userid);

    // 查找附近的用户
    vector<NearbyUser> find_nearby_users(int userid, double latitude, double longitude, 
                                        int radius_meters = 5000, int limit = 50);

    // 设置位置可见性
    bool set_location_visibility(int userid, bool is_visible);

    // 删除用户位置信息
    bool delete_user_location(int userid);

    // 记录位置历史 (可选功能)
    bool record_location_history(int userid, double latitude, double longitude, 
                                const string& location_name = "");

    // 记录搜索日志 (可选功能)
    bool log_nearby_search(int searcher_id, double latitude, double longitude, 
                          int radius, int found_count);

    // 获取用户位置历史 (可选功能)
    vector<LocationInfo> get_location_history(int userid, int limit = 20);

private:
    // 计算两点之间的距离 (Haversine公式)
    double calculate_distance(double lat1, double lng1, double lat2, double lng2);
    
    // 性能优化相关的私有方法
    bool use_spatial_optimization(int userid, double latitude, double longitude, 
                                int radius_meters, int limit, vector<NearbyUser>& nearby_users);
    vector<NearbyUser> find_nearby_users_multilayer(int userid, double latitude, double longitude, 
                                                   int radius_meters, int limit);
    
    // 空间索引支持检测和维护
    bool check_spatial_support();
    bool ensure_spatial_data_integrity();
    void print_spatial_capabilities();

    // 地球半径常量 (米)
    static const double EARTH_RADIUS_METERS;
};

#endif
