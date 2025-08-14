#include "ChatService.hpp"
#include "public.hpp"
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <iomanip>
#include <sstream>
#include "json.hpp"

using json = nlohmann::json;
using namespace std;
using namespace muduo;
using namespace muduo::net;

//更新用户位置
void ChatService::update_location(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        double latitude = js["latitude"].get<double>();
        double longitude = js["longitude"].get<double>();
        bool is_visible = js.value("is_visible", true);
        string location_name = js.value("location_name", "");
        
        json response;
        response["msgid"] = UPDATE_LOCATION_MSG;
        
        // 验证坐标范围
        if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0)
        {
            response["errno"] = 1;
            response["errmsg"] = "坐标范围无效，纬度应在[-90,90]，经度应在[-180,180]";
        }
        else
        {
            bool success = location_model_.update_user_location(userid, latitude, longitude, is_visible, location_name);
            
            if (success)
            {
                response["errno"] = 0;
                response["errmsg"] = "位置更新成功";
                response["latitude"] = latitude;
                response["longitude"] = longitude;
                response["is_visible"] = is_visible;
                response["location_name"] = location_name;
                
                // 可选：记录位置历史
                location_model_.record_location_history(userid, latitude, longitude, location_name);
            }
            else
            {
                response["errno"] = 2;
                response["errmsg"] = "位置更新失败";
            }
        }
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "update location parse error: " << e.what();
        json error_response;
        error_response["msgid"] = UPDATE_LOCATION_MSG;
        error_response["errno"] = 3;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}

//查找附近的人
void ChatService::find_nearby(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        double latitude = js["latitude"].get<double>();
        double longitude = js["longitude"].get<double>();
        int radius = js.value("radius", 5000); // 默认5000米
        int limit = js.value("limit", 20);     // 默认20个用户
        
        json response;
        response["msgid"] = FIND_NEARBY_MSG;
        
        // 验证坐标和参数
        if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0)
        {
            response["errno"] = 1;
            response["errmsg"] = "坐标范围无效";
        }
        else if (radius <= 0 || radius > 50000) // 最大50公里
        {
            response["errno"] = 2;
            response["errmsg"] = "搜索半径无效，应在1-50000米之间";
        }
        else
        {
            // 先更新当前用户的位置
            location_model_.update_user_location(userid, latitude, longitude, true);
            
            // 查找附近的用户
            auto nearby_users = location_model_.find_nearby_users(userid, latitude, longitude, radius, limit);
            
            response["errno"] = 0;
            response["errmsg"] = "查找成功";
            response["search_latitude"] = latitude;
            response["search_longitude"] = longitude;
            response["search_radius"] = radius;
            response["count"] = nearby_users.size();
            
            json users_array = json::array();
            for (const auto& nearby_user : nearby_users)
            {
                json user_json;
                user_json["userid"] = nearby_user.user.get_id();
                user_json["username"] = nearby_user.user.get_name();
                user_json["state"] = nearby_user.user.get_state();
                user_json["latitude"] = nearby_user.location.latitude;
                user_json["longitude"] = nearby_user.location.longitude;
                user_json["distance"] = round(nearby_user.distance); // 四舍五入到米
                user_json["location_name"] = nearby_user.location.location_name;
                user_json["last_update"] = nearby_user.location.last_update;
                
                // 添加距离描述
                if (nearby_user.distance < 1000)
                {
                    user_json["distance_desc"] = to_string((int)round(nearby_user.distance)) + "米";
                }
                else
                {
                    stringstream ss;
                    ss << fixed << setprecision(1) << nearby_user.distance / 1000.0 << "公里";
                    user_json["distance_desc"] = ss.str();
                }
                
                users_array.push_back(user_json);
            }
            response["nearby_users"] = users_array;
        }
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "find nearby parse error: " << e.what();
        json error_response;
        error_response["msgid"] = FIND_NEARBY_MSG;
        error_response["errno"] = 3;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}

//设置位置可见性
void ChatService::set_location_visibility(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        bool is_visible = js["is_visible"].get<bool>();
        
        json response;
        response["msgid"] = SET_LOCATION_VISIBILITY_MSG;
        
        bool success = location_model_.set_location_visibility(userid, is_visible);
        
        if (success)
        {
            response["errno"] = 0;
            response["errmsg"] = is_visible ? "已开启位置可见性" : "已关闭位置可见性";
            response["is_visible"] = is_visible;
        }
        else
        {
            response["errno"] = 1;
            response["errmsg"] = "设置失败，请先更新位置信息";
        }
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "set location visibility parse error: " << e.what();
        json error_response;
        error_response["msgid"] = SET_LOCATION_VISIBILITY_MSG;
        error_response["errno"] = 2;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}

//获取用户位置信息
void ChatService::get_location(const TcpConnectionPtr &conn, const chat::MessageWrapper &wrapper, Timestamp time)
{
    try {
        json js = json::parse(wrapper.data());
        int userid = js["id"].get<int>();
        int target_userid = js.value("target_id", userid); // 默认查询自己的位置
        
        json response;
        response["msgid"] = GET_LOCATION_MSG;
        
        // 如果查询别人的位置，需要验证权限
        if (target_userid != userid)
        {
            // 检查是否为好友关系
            vector<User> friend_list = friend_model_.query(userid);
            bool is_friend = false;
            for (const User& friend_user : friend_list)
            {
                if (friend_user.get_id() == target_userid)
                {
                    is_friend = true;
                    break;
                }
            }
            
            if (!is_friend)
            {
                response["errno"] = 1;
                response["errmsg"] = "只能查询好友的位置信息";
                string response_str = response.dump();
                conn->send(response_str);
                return;
            }
        }
        
        LocationInfo location = location_model_.get_user_location(target_userid);
        
        if (location.userid == 0)
        {
            response["errno"] = 2;
            response["errmsg"] = "用户未设置位置信息";
        }
        else if (target_userid != userid && !location.is_visible)
        {
            response["errno"] = 3;
            response["errmsg"] = "用户已关闭位置可见性";
        }
        else
        {
            response["errno"] = 0;
            response["errmsg"] = "获取成功";
            response["userid"] = location.userid;
            response["latitude"] = location.latitude;
            response["longitude"] = location.longitude;
            response["is_visible"] = location.is_visible;
            response["location_name"] = location.location_name;
            response["last_update"] = location.last_update;
        }
        
        string response_str = response.dump();
        conn->send(response_str);
        
    } catch (const exception& e) {
        LOG_ERROR << "get location parse error: " << e.what();
        json error_response;
        error_response["msgid"] = GET_LOCATION_MSG;
        error_response["errno"] = 4;
        error_response["errmsg"] = "请求格式错误";
        string error_str = error_response.dump();
        conn->send(error_str);
    }
}
