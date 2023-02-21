# DX12工程异同记录

相同部分
- 构造函数
- OnInit函数
- LoadPipeline函数

## LoadPipeline函数
> 创建适配器部分
    区分软件适配器和硬件适配器

> 创建command指令描述符
    设置描述符类型
    设置指令执行方式：直接执行、打包执行
    创建command队列
    定义和创建交换链（类似离屏缓冲区之类的一个东西？）
    禁用全屏按键（Alt + Enter）
    获取离屏缓冲区指针
    获取当前离屏缓冲区索引

> 初始化并创建描述符堆
    定义帧描述符堆
    将帧计数设置为堆描述符个数
    设置堆中描述符类型为RTV
    创建描述符堆
    刷新当前堆描述符大小（通过dxDevice获取堆描述符句柄增加数量（RTV类型）

    定义常量缓冲区描述符堆
    创建常量缓冲区描述符堆

> 创建帧资源
    获取堆顶的描述符
    根据帧计数创建一定数量的RT（RT_View)

> Command 分配器分配内存
    创建command分配器
    创建bundle分配器


## *LoadAssets函数
> 创建根签名
    检查最高根签名版本
    检查驱动支持版本，不支持就降级
    定义根签名范围（各个版本）
    初始化范围中的根签名
    定义根签名
    初始化根签名
    序列化根签名
    创建根签名

> 创建管线状态、Shader加载与编译
    创建VS和PS对象（ID3DBlob)
    编译Shader（绝对路径的VS和PS）
    定义顶点布局
    创建一堆管线状态对象&&初始化

> 创建Command链表
    先关闭创建好的链表等会reset

> 创建顶点缓冲区（类似VAO）
   创建顶点数组（后续从Mesh从而来）
   定义缓冲区大小
   创建资源堆
   把Mesh数据拷贝到缓冲区
   初始化顶点缓冲区View 

> *创建并记录bundle
    为bundle设置根签名
    为bundle设置基本片元类型（三角形）
    为bundle设置顶点缓冲区
    bundle调用DrawInstanced
    关闭bundle

> 创建围栏对象（同步对象）
    创建围栏
    创建围栏事件

## OnRender函数

> 记录command List的Command

> 执行command List
    获取commandList
> 离屏RT渲染到当前帧
    从交换链中把离屏的RT渲染到当前的帧缓冲
> 等待前一帧执行完毕



## *PopulateCommandList函数
    重置Command分配器
    重置command List    
    设置command List的一些状态
    将后台缓冲区从Persent装填转换为RT状态（只读->可写）
    获取RTV句柄
    设置渲染RT
    *渲染Bundle（前面创建并初始化好的）
    将后台缓冲区从RT状态转换为Persent装填（可写->只读）
    关闭command List

## WaitForPreviousFrame
    在command队列中插入围栏
    更新插入围栏的fence值，围栏fence计数++
    如果当前完成围栏数 < 当前同步围栏数，视为当前围栏未全部完成

