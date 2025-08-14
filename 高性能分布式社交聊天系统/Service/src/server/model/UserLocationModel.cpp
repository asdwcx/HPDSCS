#include "UserLocationModel.hpp"
#include "MySQL.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

using namespace std;

// 地球半径常量 (米)
const double UserLocationModel::EARTH_RADIUS_METERS = 6371000.0;

// 更新用户位置信息 - 支持空间索引优化
bool UserLocationModel::update_user_location(int userid, double latitude, double longitude, 
                                            bool is_visible, const string& location_name)
{
    // 验证坐标有效性
    if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0) {
        cout << "Invalid coordinates: lat=" << latitude << ", lng=" << longitude << endl;
        return false;
    }
    
    char sql[2048] = {0};
    
    // 优先使用Generated Column版本的SQL（MySQL 5.7+）
    sprintf(sql, 
        "INSERT INTO UserLocation(userid, latitude, longitude, is_visible, location_name) "
        "VALUES(%d, %.8f, %.8f, %s, '%s') "
        "ON DUPLICATE KEY UPDATE "
        "latitude = %.8f, longitude = %.8f, is_visible = %s, location_name = '%s', "
        "last_update = CURRENT_TIMESTAMP;",
        userid, latitude, longitude, is_visible ? "TRUE" : "FALSE", location_name.c_str(),
        latitude, longitude, is_visible ? "TRUE" : "FALSE", location_name.c_str());
    
    MySQL mysql;
    if (!mysql.connet()) {
        cout << "Failed to connect to database for location update" << endl;
        return false;
    }
    
    bool result = mysql.update(sql);
    
    if (result) {
        // 验证空间点是否正确更新（仅在调试模式下）
        #ifdef DEBUG
        char verify_sql[1024] = {0};
        sprintf(verify_sql, 
            "SELECT ST_X(location_point) as lng, ST_Y(location_point) as lat "
            "FROM UserLocation WHERE userid = %d AND location_point IS NOT NULL;", userid);
        
        MYSQL_RES *verify_res = mysql.query(verify_sql);
        if (verify_res != nullptr) {
            MYSQL_ROW verify_row = mysql_fetch_row(verify_res);
            if (verify_row) {
                double stored_lng = atof(verify_row[0]);
                double stored_lat = atof(verify_row[1]);
                cout << "Spatial point verified: stored(" << stored_lat << ", " << stored_lng 
                     << ") vs input(" << latitude << ", " << longitude << ")" << endl;
            }
            mysql_free_result(verify_res);
        }
        #endif
        
        cout << "Location updated for user " << userid 
             << " at (" << latitude << ", " << longitude << ")" 
             << (is_visible ? " [visible]" : " [hidden]") << endl;
    } else {
        cout << "Failed to update location for user " << userid << endl;
    }
    
    return result;
}

// 获取用户当前位置
LocationInfo UserLocationModel::get_user_location(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "SELECT userid, latitude, longitude, last_update, is_visible, location_name "
                 "FROM UserLocation WHERE userid = %d;", userid);
    
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                LocationInfo location(
                    atoi(row[0]),           // userid
                    atof(row[1]),           // latitude
                    atof(row[2]),           // longitude
                    row[3] ? row[3] : "",   // last_update
                    row[4] && strcmp(row[4], "1") == 0, // is_visible
                    row[5] ? row[5] : ""    // location_name
                );
                mysql_free_result(res);
                return location;
            }
            mysql_free_result(res);
        }
    }
    return LocationInfo(); // 返回空的位置信息
}

// 查找附近的用户 - 优化版本
vector<NearbyUser> UserLocationModel::find_nearby_users(int userid, double latitude, double longitude, 
                                                       int radius_meters, int limit)
{
    // 记录搜索日志
    log_nearby_search(userid, latitude, longitude, radius_meters, 0);
    
    vector<NearbyUser> nearby_users;
    
    // 方案1：尝试使用MySQL空间函数（如果支持）
    if (use_spatial_optimization(userid, latitude, longitude, radius_meters, limit, nearby_users)) {
        log_nearby_search(userid, latitude, longitude, radius_meters, nearby_users.size());
        return nearby_users;
    }
    
    // 方案2：多层筛选优化
    return find_nearby_users_multilayer(userid, latitude, longitude, radius_meters, limit);
}

// 使用MySQL空间函数的优化实现
bool UserLocationModel::use_spatial_optimization(int userid, double latitude, double longitude, 
                                                int radius_meters, int limit, vector<NearbyUser>& nearby_users)
{
    MySQL mysql;
    if (!mysql.connet()) {
        cout << "Failed to connect to database for spatial optimization" << endl;
        return false;
    }
    
    // 检查是否支持空间函数和索引
    const char* check_spatial = "SHOW INDEX FROM UserLocation WHERE Key_name = 'spatial_idx'";
    MYSQL_RES* check_res = mysql.query(check_spatial);
    bool has_spatial_index = (check_res != nullptr && mysql_num_rows(check_res) > 0);
    if (check_res) mysql_free_result(check_res);
    
    if (!has_spatial_index) {
        cout << "Spatial index not found, falling back to multilayer algorithm" << endl;
        return false;
    }
    
    // 检查是否支持空间函数（测试查询）
    const char* test_spatial = "SELECT ST_Distance_Sphere(POINT(0, 0), POINT(1, 1)) as test_distance";
    MYSQL_RES* test_res = mysql.query(test_spatial);
    if (test_res == nullptr) {
        cout << "Spatial functions not supported, falling back to multilayer algorithm" << endl;
        return false;
    }
    mysql_free_result(test_res);
    
    // 构建优化的空间查询
    char sql[2048] = {0};
    sprintf(sql, 
        "SELECT ul.userid, ul.latitude, ul.longitude, ul.last_update, "
        "ul.is_visible, ul.location_name, u.name, u.state, "
        "ST_Distance_Sphere(ul.location_point, POINT(%.8f, %.8f)) as distance "
        "FROM UserLocation ul "
        "JOIN User u ON ul.userid = u.id "
        "WHERE ul.userid != %d AND ul.is_visible = TRUE "
        "AND ul.location_point IS NOT NULL "
        "AND ST_Distance_Sphere(ul.location_point, POINT(%.8f, %.8f)) <= %d "
        "ORDER BY distance LIMIT %d;",
        longitude, latitude,  // POINT参数
        userid,
        longitude, latitude,  // 距离计算参数
        radius_meters,
        limit
    );
    
    cout << "Executing spatial query for user " << userid 
         << " at (" << latitude << ", " << longitude 
         << ") within " << radius_meters << "m" << endl;
    
    auto start_time = chrono::high_resolution_clock::now();
    
    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr) {
        cout << "Spatial query failed: " << mysql_error(mysql.get_connection()) << endl;
        return false;
    }
    
    MYSQL_ROW row;
    int result_count = 0;
    
    while ((row = mysql_fetch_row(res)) != nullptr) {
        User user;
        user.set_id(atoi(row[0]));
        user.set_name(row[6] ? row[6] : "");
        user.set_state(row[7] ? row[7] : "offline");
        
        LocationInfo location(
            atoi(row[0]),           // userid
            atof(row[1]),           // latitude
            atof(row[2]),           // longitude
            row[3] ? row[3] : "",   // last_update
            true,                   // is_visible
            row[5] ? row[5] : ""    // location_name
        );
        
        double distance = atof(row[8]); // 使用数据库计算的距离
        nearby_users.emplace_back(user, location, distance);
        result_count++;
    }
    
    mysql_free_result(res);
    
    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
    
    cout << "Spatial query completed: found " << result_count 
         << " users in " << duration.count() << "ms" << endl;
    
    return true;
}

// 多层筛选的备用实现
vector<NearbyUser> UserLocationModel::find_nearby_users_multilayer(int userid, double latitude, double longitude, 
                                                                  int radius_meters, int limit)
{
    vector<NearbyUser> nearby_users;
    
    // 第1层：智能矩形预筛选（紧密贴合圆形区域）
    double lat_range = (double)radius_meters / EARTH_RADIUS_METERS * 180.0 / M_PI;
    double lng_range = lat_range / cos(latitude * M_PI / 180.0);
    
    // 第2层：粗略距离筛选（避免复杂三角函数）
    double rough_distance_threshold = radius_meters * 1.1; // 稍微放宽，避免边界误差
    
    char sql[2048] = {0};
    sprintf(sql, 
        "SELECT ul.userid, ul.latitude, ul.longitude, ul.last_update, "
        "ul.is_visible, ul.location_name, u.name, u.state "
        "FROM UserLocation ul "
        "JOIN User u ON ul.userid = u.id "
        "WHERE ul.userid != %d AND ul.is_visible = TRUE "
        "AND ul.latitude BETWEEN %.8f AND %.8f "
        "AND ul.longitude BETWEEN %.8f AND %.8f "
        "ORDER BY ul.last_update DESC LIMIT %d;",
        userid,
        latitude - lat_range, latitude + lat_range,
        longitude - lng_range, longitude + lng_range,
        limit * 3  // 预取更多候选，但不会太多
    );
    
    MySQL mysql;
    if (!mysql.connet()) {
        return nearby_users;
    }
    
    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr) {
        return nearby_users;
    }
    
    vector<NearbyUser> candidates;
    MYSQL_ROW row;
    
    // 第2层：快速距离筛选（使用简化的距离计算）
    while ((row = mysql_fetch_row(res)) != nullptr) {
        double user_lat = atof(row[1]);
        double user_lng = atof(row[2]);
        
        // 快速距离估算（曼哈顿距离的球面版本）
        double lat_diff = abs(user_lat - latitude);
        double lng_diff = abs(user_lng - longitude);
        double rough_distance = sqrt(lat_diff * lat_diff + lng_diff * lng_diff) * EARTH_RADIUS_METERS * M_PI / 180.0;
        
        // 如果粗略距离就超出范围，直接跳过
        if (rough_distance > rough_distance_threshold) {
            continue;
        }
        
        User user;
        user.set_id(atoi(row[0]));
        user.set_name(row[6] ? row[6] : "");
        user.set_state(row[7] ? row[7] : "offline");
        
        LocationInfo location(
            atoi(row[0]),           // userid
            user_lat,               // latitude
            user_lng,               // longitude
            row[3] ? row[3] : "",   // last_update
            true,                   // is_visible
            row[5] ? row[5] : ""    // location_name
        );
        
        candidates.emplace_back(user, location, rough_distance);
    }
    mysql_free_result(res);
    
    // 第3层：精确Haversine计算（仅对候选结果，数量已大幅减少）
    for (auto& candidate : candidates) {
        double precise_distance = calculate_distance(latitude, longitude, 
                                                   candidate.location.latitude, 
                                                   candidate.location.longitude);
        
        // 精确距离检查
        if (precise_distance <= radius_meters) {
            candidate.distance = precise_distance; // 更新为精确距离
            nearby_users.push_back(candidate);
        }
    }
    
    // 按精确距离排序
    sort(nearby_users.begin(), nearby_users.end(), 
         [](const NearbyUser& a, const NearbyUser& b) {
             return a.distance < b.distance;
         });
    
    // 限制返回数量
    if (nearby_users.size() > (size_t)limit) {
        nearby_users.resize(limit);
    }
    
    return nearby_users;
}

// 设置位置可见性
bool UserLocationModel::set_location_visibility(int userid, bool is_visible)
{
    char sql[1024] = {0};
    sprintf(sql, "UPDATE UserLocation SET is_visible = %s WHERE userid = %d;",
            is_visible ? "TRUE" : "FALSE", userid);
    
    MySQL mysql;
    if (mysql.connet())
    {
        return mysql.update(sql);
    }
    return false;
}

// 删除用户位置信息
bool UserLocationModel::delete_user_location(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "DELETE FROM UserLocation WHERE userid = %d;", userid);
    
    MySQL mysql;
    if (mysql.connet())
    {
        return mysql.update(sql);
    }
    return false;
}

// 记录位置历史
bool UserLocationModel::record_location_history(int userid, double latitude, double longitude, 
                                               const string& location_name)
{
    char sql[1024] = {0};
    sprintf(sql, "INSERT INTO UserLocationHistory(userid, latitude, longitude, location_name) "
                 "VALUES(%d, %.8f, %.8f, '%s');",
                 userid, latitude, longitude, location_name.c_str());
    
    MySQL mysql;
    if (mysql.connet())
    {
        return mysql.update(sql);
    }
    return false;
}

// 记录搜索日志
bool UserLocationModel::log_nearby_search(int searcher_id, double latitude, double longitude, 
                                         int radius, int found_count)
{
    char sql[1024] = {0};
    sprintf(sql, "INSERT INTO NearbySearchLog(searcher_id, search_latitude, search_longitude, "
                 "search_radius, found_count) VALUES(%d, %.8f, %.8f, %d, %d);",
                 searcher_id, latitude, longitude, radius, found_count);
    
    MySQL mysql;
    if (mysql.connet())
    {
        return mysql.update(sql);
    }
    return false;
}

// 获取用户位置历史
vector<LocationInfo> UserLocationModel::get_location_history(int userid, int limit)
{
    char sql[1024] = {0};
    sprintf(sql, "SELECT userid, latitude, longitude, update_time, 1, location_name "
                 "FROM UserLocationHistory WHERE userid = %d "
                 "ORDER BY update_time DESC LIMIT %d;", userid, limit);
    
    vector<LocationInfo> history;
    MySQL mysql;
    if (mysql.connet())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                LocationInfo location(
                    atoi(row[0]),           // userid
                    atof(row[1]),           // latitude
                    atof(row[2]),           // longitude
                    row[3] ? row[3] : "",   // update_time
                    true,                   // is_visible
                    row[5] ? row[5] : ""    // location_name
                );
                history.push_back(location);
            }
            mysql_free_result(res);
        }
    }
    return history;
}

// 计算两点之间的距离 (Haversine公式)
double UserLocationModel::calculate_distance(double lat1, double lng1, double lat2, double lng2)
{
    // 将角度转换为弧度
    double lat1_rad = lat1 * M_PI / 180.0;
    double lng1_rad = lng1 * M_PI / 180.0;
    double lat2_rad = lat2 * M_PI / 180.0;
    double lng2_rad = lng2 * M_PI / 180.0;
    
    // Haversine公式计算球面距离
    double dlat = lat2_rad - lat1_rad;
    double dlng = lng2_rad - lng1_rad;
    
    double a = sin(dlat/2) * sin(dlat/2) + 
               cos(lat1_rad) * cos(lat2_rad) * sin(dlng/2) * sin(dlng/2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));
    
    return EARTH_RADIUS_METERS * c; // 返回距离(米)
}

// 检查MySQL空间索引支持
bool UserLocationModel::check_spatial_support() {
    MySQL mysql;
    if (!mysql.connet()) {
        cout << "Failed to connect to database for spatial support check" << endl;
        return false;
    }
    
    bool has_spatial_functions = false;
    bool has_spatial_index = false;
    bool has_location_point_column = false;
    
    // 1. 检查空间函数支持
    const char* test_spatial_func = "SELECT ST_Distance_Sphere(POINT(0, 0), POINT(1, 1)) as test";
    MYSQL_RES* func_res = mysql.query(test_spatial_func);
    if (func_res != nullptr) {
        has_spatial_functions = true;
        mysql_free_result(func_res);
    }
    
    // 2. 检查location_point列是否存在
    const char* check_column = "SHOW COLUMNS FROM UserLocation LIKE 'location_point'";
    MYSQL_RES* col_res = mysql.query(check_column);
    if (col_res != nullptr && mysql_num_rows(col_res) > 0) {
        has_location_point_column = true;
        mysql_free_result(col_res);
    }
    
    // 3. 检查空间索引是否存在
    const char* check_index = "SHOW INDEX FROM UserLocation WHERE Key_name = 'spatial_idx'";
    MYSQL_RES* idx_res = mysql.query(check_index);
    if (idx_res != nullptr && mysql_num_rows(idx_res) > 0) {
        has_spatial_index = true;
        mysql_free_result(idx_res);
    }
    
    cout << "=== MySQL Spatial Support Status ===" << endl;
    cout << "Spatial Functions: " << (has_spatial_functions ? "✓ Supported" : "✗ Not Supported") << endl;
    cout << "Location Point Column: " << (has_location_point_column ? "✓ Exists" : "✗ Missing") << endl;
    cout << "Spatial Index: " << (has_spatial_index ? "✓ Exists" : "✗ Missing") << endl;
    
    bool full_support = has_spatial_functions && has_location_point_column && has_spatial_index;
    cout << "Full Spatial Optimization: " << (full_support ? "✓ Available" : "✗ Not Available") << endl;
    
    if (!full_support) {
        cout << "Run upgrade_mysql_spatial_optimization.sql to enable spatial optimization" << endl;
    }
    
    return full_support;
}

// 确保空间数据完整性
bool UserLocationModel::ensure_spatial_data_integrity() {
    MySQL mysql;
    if (!mysql.connet()) {
        return false;
    }
    
    // 检查有多少记录的location_point为NULL
    const char* check_null_points = 
        "SELECT COUNT(*) as null_count FROM UserLocation WHERE location_point IS NULL";
    
    MYSQL_RES* res = mysql.query(check_null_points);
    int null_count = 0;
    
    if (res != nullptr) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            null_count = atoi(row[0]);
        }
        mysql_free_result(res);
    }
    
    if (null_count > 0) {
        cout << "Found " << null_count << " records with NULL location_point, updating..." << endl;
        
        // 更新所有NULL的location_point
        const char* update_points = 
            "UPDATE UserLocation SET location_point = POINT(longitude, latitude) "
            "WHERE location_point IS NULL";
        
        if (mysql.update(update_points)) {
            cout << "Successfully updated " << null_count << " spatial points" << endl;
            return true;
        } else {
            cout << "Failed to update spatial points" << endl;
            return false;
        }
    } else {
        cout << "All spatial points are up to date" << endl;
        return true;
    }
}

// 打印空间能力信息
void UserLocationModel::print_spatial_capabilities() {
    cout << "\n=== UserLocationModel Spatial Capabilities ===" << endl;
    
    MySQL mysql;
    if (!mysql.connet()) {
        cout << "✗ Database connection failed" << endl;
        return;
    }
    
    // 获取MySQL版本
    const char* version_query = "SELECT VERSION() as mysql_version";
    MYSQL_RES* ver_res = mysql.query(version_query);
    if (ver_res != nullptr) {
        MYSQL_ROW ver_row = mysql_fetch_row(ver_res);
        if (ver_row) {
            cout << "MySQL Version: " << ver_row[0] << endl;
        }
        mysql_free_result(ver_res);
    }
    
    // 获取表统计信息
    const char* stats_query = 
        "SELECT COUNT(*) as total_users, "
        "COUNT(CASE WHEN location_point IS NOT NULL THEN 1 END) as with_spatial_point, "
        "COUNT(CASE WHEN is_visible = TRUE THEN 1 END) as visible_users "
        "FROM UserLocation";
    
    MYSQL_RES* stats_res = mysql.query(stats_query);
    if (stats_res != nullptr) {
        MYSQL_ROW stats_row = mysql_fetch_row(stats_res);
        if (stats_row) {
            cout << "Total Users: " << stats_row[0] << endl;
            cout << "Users with Spatial Points: " << stats_row[1] << endl;
            cout << "Visible Users: " << stats_row[2] << endl;
        }
        mysql_free_result(stats_res);
    }
    
    // 检查索引信息
    const char* index_query = 
        "SELECT INDEX_NAME, CARDINALITY, INDEX_TYPE "
        "FROM INFORMATION_SCHEMA.STATISTICS "
        "WHERE TABLE_NAME = 'UserLocation' AND TABLE_SCHEMA = DATABASE() "
        "ORDER BY INDEX_NAME";
    
    cout << "\nIndexes:" << endl;
    MYSQL_RES* idx_res = mysql.query(index_query);
    if (idx_res != nullptr) {
        MYSQL_ROW idx_row;
        while ((idx_row = mysql_fetch_row(idx_res)) != nullptr) {
            cout << "  " << idx_row[0] << " (" << idx_row[2] << ")"
                 << " - Cardinality: " << (idx_row[1] ? idx_row[1] : "NULL") << endl;
        }
        mysql_free_result(idx_res);
    }
    
    cout << "==========================================\n" << endl;
}
