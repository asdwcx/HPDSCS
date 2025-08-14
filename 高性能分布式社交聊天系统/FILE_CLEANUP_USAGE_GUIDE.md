# 自动文件清理系统使用指南

## 快速开始

您的聊天服务器现在已经集成了自动文件清理功能，可以防止临时文件累积导致存储空间不足。

### 1. 安装配置

**Linux/Mac:**
```bash
cd Service
./manage_file_cleanup.sh install
```

**Windows:**
```cmd
cd Service
manage_file_cleanup.bat install
```

### 2. 构建服务器

**Linux/Mac:**
```bash
# 构建整个系统
./build_with_cleanup.sh build

# 或者使用现有构建系统，确保包含新的源文件
cd Service
make  # 或使用您的构建脚本
```

**Windows:**
```cmd
REM 在 Visual Studio Developer Command Prompt 中运行
build_with_cleanup.bat build
```

### 3. 启动服务器

服务器启动时会自动初始化和启动文件清理服务：

**Linux/Mac:**
```bash
cd Service
./start_server.sh
```

**Windows:**
```cmd
cd Service
start_server.bat
```

## 配置说明

配置文件位置: `Service/config/file_cleanup.json`

```json
{
    "cleanup_interval_seconds": 300,           // 每5分钟清理一次
    "temp_file_max_age_seconds": 3600,         // 保留1小时
    "max_directory_size_bytes": 1073741824,    // 最大1GB
    "temp_directories": [
        "./temp",
        "./uploads/temp"
    ],
    "log_file": "./logs/file_cleanup.log",
    "enable_logging": true,
    "enable_statistics": true,
    "cleanup_on_startup": true
}
```

## 管理操作

### 查看状态
```bash
./manage_file_cleanup.sh check-dirs    # 检查目录状态
./manage_file_cleanup.sh config        # 查看配置
```

### 手动清理
```bash
./manage_file_cleanup.sh manual-cleanup    # 立即执行清理
./manage_file_cleanup.sh test-cleanup      # 测试清理功能
```

## 监控日志

清理日志位置: `Service/logs/file_cleanup.log`

```bash
# 查看实时日志
tail -f Service/logs/file_cleanup.log

# 查看最近的清理记录
grep "cleanup completed" Service/logs/file_cleanup.log | tail -10
```

## 系统集成

文件清理服务已完全集成到以下组件中：

1. **DistributedServiceCoordinator** - 服务协调器会自动管理清理服务的生命周期
2. **ChatService** - 聊天服务集成了清理功能配置
3. **主服务启动流程** - 服务器启动时自动初始化清理服务

## 故障排除

### 常见问题

**问题1: 清理服务启动失败**
```
解决方法:
1. 检查配置文件是否存在且格式正确
2. 确认目录权限
3. 查看日志文件获取详细错误信息
```

**问题2: 文件无法删除**
```
解决方法:
1. 检查文件是否被其他进程占用
2. 确认删除权限
3. 检查文件路径是否正确
```

**问题3: 清理效果不明显**
```
解决方法:
1. 调整 temp_file_max_age_seconds 参数
2. 增加 cleanup_interval_seconds 频率
3. 检查 temp_directories 配置是否正确
```

### 调试步骤

1. **检查服务状态**
   ```bash
   # 在程序运行时，清理服务会自动工作
   # 可以通过日志文件监控其工作状态
   ```

2. **手动测试**
   ```bash
   ./manage_file_cleanup.sh test-cleanup
   ```

3. **查看日志**
   ```bash
   cat Service/logs/file_cleanup.log
   ```

## 性能说明

- **CPU 使用**: 清理服务在后台运行，CPU 占用极低
- **内存使用**: 约占用 1-2MB 内存
- **I/O 影响**: 清理操作在低优先级下执行，不会影响主要服务
- **网络影响**: 无网络操作，仅本地文件管理

## 安全特性

- ✅ 路径验证防止误删系统文件
- ✅ 文件锁检查避免删除使用中的文件  
- ✅ 线程安全的并发控制
- ✅ 完善的错误处理和日志记录
- ✅ 可配置的安全参数

## 自定义配置

根据您的需求调整以下参数：

**存储空间较小的服务器:**
```json
{
    "cleanup_interval_seconds": 180,     // 3分钟清理一次
    "temp_file_max_age_seconds": 1800,   // 保留30分钟
    "max_directory_size_bytes": 536870912 // 最大512MB
}
```

**高负载服务器:**
```json
{
    "cleanup_interval_seconds": 600,     // 10分钟清理一次
    "temp_file_max_age_seconds": 7200,   // 保留2小时
    "max_directory_size_bytes": 2147483648 // 最大2GB
}
```

现在您的聊天服务器具备了自动文件清理功能，可以有效防止临时文件累积导致的存储问题！🎉
