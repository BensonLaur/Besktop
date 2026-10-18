# 三维动画验证的第三方来源与许可

本目录文档、Besktop 新增代码与自编排动作遵循仓库许可证；第三方数据和工具保持各自许可，不能将 CMU 动捕改称 Besktop 自制或重新标成 GPL / CC0。

## CMU 动捕与 BVH 转换

- 原始数据：[CMU Graphics Lab Motion Capture Database](https://mocap.cs.cmu.edu/)。
- 复制、修改和再分发说明：[CMU FAQ](https://mocap.cs.cmu.edu/faqs.php)。
- 最终片段：`08_01`（行走）、`113_21`（站立）、`16_33`（慢走后停止）、`16_19`（行走中向右转 90°）。停止与转向的分类见 [CMU subject 16](https://mocap.cs.cmu.edu/search.php?subjectnumber=16)。
- 格式转换：Bruce Hahne / cgspeed 的 [MotionBuilder 2010 BVH](https://sites.google.com/a/cgspeed.com/cgspeed/motion-capture/the-motionbuilder-friendly-bvh-conversion-release-of-cmus-motion-capture-database/readme-motionbuilder-2010-release)。首帧是转换者估计的参考 T 姿态，不是真实动捕；真实数据从第二帧开始，采样率为 120 Hz。
- 下载：[Codewelt 镜像](https://codewelt.com/cmumocap/mb2)。不是 CMU 官方分发服务器；实际文件 SHA-256 固定在 `tools/animation/dependencies.json`，用于后续复现，不等于 CMU 的数字签名。
- CMU 官网允许用于研究及商业产品，同时明确禁止将动作数据本身直接转售，包括转换后的形式。cgspeed 表示不对转换另加限制。购买更多素材时必须重新核对原始文件及衍生数据分发权。
- 本轮原始 BVH、转换后的动作及 Blender 工程保存在被忽略的 `out/`；公开源码保存流程、来源与校验值。实验 EXE 嵌入的是运行所需的转换数据，不包含 Blender 或原始 BVH。

致谢：The data used in this project was obtained from mocap.cs.cmu.edu. The database was created with funding from NSF EIA-0196217.

庆祝、握手和拥抱是 Besktop 的验证编排，不是 CMU 的对应动捕片段；不得在报告或对外说明中混称为全套真实动捕。

筛选时还检查过 `131_01` 和 `102_01`。回看样片与主体分类后确认，它们分别属于 Michael Jackson 风格表演和篮球动作，不适合作为自然日常停止/转身的首批基准，因此未收入最终资源包。

## 人体比例数值参考

腿段分配使用 [OpenSim Rajagopal2016 公开参考模型](https://github.com/opensim-org/opensim-models/blob/d9b05d470b1a481c222372c85b75772faf8f7792/Models/Rajagopal/Rajagopal2016.osim) 中少量中立位框架端点数值，出处为 [Rajagopal et al. (2016)](https://doi.org/10.1109/TBME.2016.2586891)。来源提交、文件哈希、测量口径和缩放见 [人体比例依据](ANTHROPOMETRY.md)。本项目没有集成 OpenSim 软件，没有复制/分发其完整模型、几何或肌肉数据；本轮数值参考不等于获得第三方整包资产的再分发授权。国标 PDF 和论文原文也不随预览包分发。

## Blender 制作工具

制作端固定使用 Blender 4.5.3 LTS 便携包。它只放在项目本地缓存，没有系统安装、文件关联或随 EXE 分发。下载由 Blender 官方镜像入口指向镜像，ZIP 使用官方发布的 SHA-256 核对。

## ozz-animation 0.16.0

源码固定为 `6cbdc790123aa4731d82e255df187b3a8a808256`，运行时静态链接，仅用于骨架和动作采样。离线使用 `gltf2ozz`，不将离线转换工具或其依赖分发给最终用户。

以下保留 [ozz-animation MIT 许可证](https://github.com/guillaumeblanc/ozz-animation/blob/0.16.0/LICENSE.md) 原文：

```text
Copyright (c) 2020 Guillaume Blanc

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

发布任何包含 ozz 的实验二进制时，一并保留本通知。新资源包与旧 Pack MVP 是不同格式；当前实验不接受未受信任的任意 ozz 文件。
