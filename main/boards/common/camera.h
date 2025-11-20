#ifndef CAMERA_H
#define CAMERA_H

#include <string>

/**
 * @class Camera
 * @brief 摄像头硬件的抽象基类。
 *
 * @details
 * 这个类定义了所有具体摄像头实现必须遵循的通用接口。
 * 它通过纯虚函数强制子类提供核心功能，如图像捕获、参数设置和AI视觉解释。
 * 这种设计使得上层应用可以与摄像头硬件解耦，方便适配不同的摄像头模组和驱动。
 */
class Camera {
public:
    /**
     * @brief 设置AI视觉解释服务的URL和认证令牌。
     * @param url 服务器的URL地址。
     * @param token 用于认证的Bearer Token。
     */
    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;

    /**
     * @brief 捕获一帧图像。
     * @details
     * 实现此方法时，应从摄像头硬件获取一帧图像数据，
     * 并将其处理后（如格式转换、旋转等）用于后续操作，例如在屏幕上显示预览。
     * @return bool 如果捕获成功，返回true；否则返回false。
     */
    virtual bool Capture() = 0;

    /**
     * @brief 设置图像水平镜像。
     * @param enabled true表示开启镜像，false表示关闭。
     * @return bool 如果设置成功，返回true。
     */
    virtual bool SetHMirror(bool enabled) = 0;

    /**
     * @brief 设置图像垂直翻转。
     * @param enabled true表示开启翻转，false表示关闭。
     * @return bool 如果设置成功，返回true。
     */
    virtual bool SetVFlip(bool enabled) = 0;

    /**
     * @brief 将当前捕获的图像发送到AI服务进行分析。
     * @details
     * 此方法封装了“拍照-编码-上传-获取结果”的完整流程。
     * 它应该将内部缓存的最新一帧图像编码（如JPEG），然后通过HTTP POST请求
     * 发送给由 `SetExplainUrl` 指定的服务器，并附带用户的问题。
     * @param question 用户提出的关于图像内容的问题。
     * @return std::string 服务器返回的JSON格式的分析结果。
     */
    virtual std::string Explain(const std::string& question) = 0;
};

#endif // CAMERA_H
