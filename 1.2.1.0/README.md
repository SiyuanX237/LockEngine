# LockEngine(旧版v1.2.1.0)

### 适用于Win10及以上的锁屏壁纸引擎工具
### 作者：[Bilibili - 个人隐思](https://space.bilibili.com/1081364881 "来我主页玩玩ヾ(^∀^)ﾉ")
### 爱发电主页：[ThinkAlone](https://afdian.com/a/X1415 "您赞助的每一分都是我前进的动力")
编程不易，打赏随意：

<img src="../images/afdian-ThinkAlone.jpg" height="300" /> <img src="../images/mm_reward.png" height="300" />

## 功能特点
- 不修改系统文件，退出即恢复
- 使用开源libVLC 3.0.19
- 自适应锁屏尺寸，拒绝四周黑边
- 伪随机播放，不重不漏
- 独创独立息屏控制(beta)，可以自由控制锁屏界面的息屏时间，仅修改锁屏界面，退出即恢复

## 使用方法：
首次使用会自动生成Config.ini和PlayList文件夹。只需简单设置并把待播放视频放入PlayList中，即可使用。息屏超时时间可在Config.ini内修改，单位是秒，默认时间为一分钟

## 原理：
通过查找锁屏界面的背景图层窗口LockScreenBackstopFrame的Backstop Window的句柄，控制其透明化，并在桌面生成一个顶层窗口用于播放视频

## 视频介绍
[\[首发\]适用于Win10及以上的动态锁屏壁纸引擎软件](https://www.bilibili.com/video/BV1shJQzQELF/)

# 祝你使用愉快:P
