// 位置服务客户端实现函数

// 更新位置
void UpdateLocation(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "UpdateLocation command invalid! Format: updatelocation:latitude:longitude:location_name" << endl;
        return;
    }
    
    string params = str.substr(idx + 1);
    vector<string> parts;
    stringstream ss(params);
    string item;
    
    while (getline(ss, item, ':'))
    {
        parts.push_back(item);
    }
    
    if (parts.size() < 2)
    {
        cerr << "UpdateLocation command invalid! Format: updatelocation:latitude:longitude:location_name" << endl;
        return;
    }
    
    try
    {
        double latitude = stod(parts[0]);
        double longitude = stod(parts[1]);
        string location_name = parts.size() > 2 ? parts[2] : "";
        
        json js;
        js["msgid"] = UPDATE_LOCATION_MSG;
        js["id"] = g_current_user.get_id();
        js["latitude"] = latitude;
        js["longitude"] = longitude;
        js["is_visible"] = true;
        js["location_name"] = location_name;
        
        string buffer = js.dump();
        
        int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "send update location msg error -> " << buffer << endl;
        }
        else
        {
            cout << "位置更新请求已发送" << endl;
        }
    }
    catch (const exception& e)
    {
        cerr << "坐标格式错误: " << e.what() << endl;
    }
}

// 查找附近的人
void FindNearby(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "FindNearby command invalid! Format: findnearby:latitude:longitude:radius" << endl;
        return;
    }
    
    string params = str.substr(idx + 1);
    vector<string> parts;
    stringstream ss(params);
    string item;
    
    while (getline(ss, item, ':'))
    {
        parts.push_back(item);
    }
    
    if (parts.size() < 2)
    {
        cerr << "FindNearby command invalid! Format: findnearby:latitude:longitude:radius" << endl;
        return;
    }
    
    try
    {
        double latitude = stod(parts[0]);
        double longitude = stod(parts[1]);
        int radius = parts.size() > 2 ? stoi(parts[2]) : 5000; // 默认5000米
        
        json js;
        js["msgid"] = FIND_NEARBY_MSG;
        js["id"] = g_current_user.get_id();
        js["latitude"] = latitude;
        js["longitude"] = longitude;
        js["radius"] = radius;
        js["limit"] = 20;
        
        string buffer = js.dump();
        
        int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "send find nearby msg error -> " << buffer << endl;
        }
        else
        {
            cout << "查找附近的人请求已发送，搜索半径: " << radius << "米" << endl;
        }
    }
    catch (const exception& e)
    {
        cerr << "参数格式错误: " << e.what() << endl;
    }
}

// 设置位置可见性
void SetLocationVisibility(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "SetLocationVisibility command invalid! Format: setvisibility:visible(1显示/0隐藏)" << endl;
        return;
    }
    
    string visible_str = str.substr(idx + 1);
    
    try
    {
        int visible = stoi(visible_str);
        if (visible != 0 && visible != 1)
        {
            cerr << "Visibility value must be 0 or 1!" << endl;
            return;
        }
        
        json js;
        js["msgid"] = SET_LOCATION_VISIBILITY_MSG;
        js["id"] = g_current_user.get_id();
        js["is_visible"] = (visible == 1);
        
        string buffer = js.dump();
        
        int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "send set location visibility msg error -> " << buffer << endl;
        }
        else
        {
            cout << "位置可见性设置请求已发送: " << (visible ? "显示" : "隐藏") << endl;
        }
    }
    catch (const exception& e)
    {
        cerr << "参数格式错误: " << e.what() << endl;
    }
}

// 获取位置信息
void GetLocation(int clientfd, string str)
{
    int target_id = g_current_user.get_id(); // 默认查询自己
    
    int idx = str.find(":");
    if (idx != -1)
    {
        string target_str = str.substr(idx + 1);
        try
        {
            target_id = stoi(target_str);
        }
        catch (const exception& e)
        {
            cerr << "用户ID格式错误: " << e.what() << endl;
            return;
        }
    }
    
    json js;
    js["msgid"] = GET_LOCATION_MSG;
    js["id"] = g_current_user.get_id();
    js["target_id"] = target_id;
    
    string buffer = js.dump();
    
    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "send get location msg error -> " << buffer << endl;
    }
    else
    {
        if (target_id == g_current_user.get_id())
        {
            cout << "查询自己位置信息请求已发送" << endl;
        }
        else
        {
            cout << "查询用户 " << target_id << " 位置信息请求已发送" << endl;
        }
    }
}
