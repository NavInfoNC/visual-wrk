@[TOC](Linux平台下可视化压测软件visual-wrk)

	**visual-wrk**是一款基于wrk开源项目二次开发的Linux下可视化压测软件（git地址 https://github.com/NavInfo2/visual-wrk.git）

	朋友，如果你需要一款Linux下**安装简便、易于使用**的压测工具；或是需要**超高并发**却苦于没有服务器集群；或是需要将压力测试**集成至Jenkins**并得到一份**丰富的可视化报表**；又或是需要**随机、按比例混合**的压测工具，那么**visual-wrk**是你的不二之选！！！

## 为什么基于wrk做二次开发？
	在公司的测试环境中，考虑该压测软件将与Linux上的Jenkins持续集成工具联动使用；另外考虑到公司的技术栈以C/C++为主，因此我们需要一款Linux下**安装方便操作简单**、**高并发**、**高扩展性**、使用C/C++编写的开源命令行压测工具。

	以下是对部分压测软件的功能比较：
	| 压测软件    |  并发量 | https  |  POST  | 报告完善度 | 可扩展性 | 并发架构
	| --------    | :-----: | :----: | :----: |   :----:   |  :----:  |  :----:  |
	| http_load   | 中      |   Y    |   N    |    中      |    中    | select
	| webbench    | 高      |   Y    |   N    |    中      |    中    | fork
	| ab          | 高      |   Y    |   Y    |    中      |    中    | epoll
	| siege       | 低      |   Y    |   Y    |    中      |    中    | multi thread
	| wrk         | 高      |   Y    |   Y    |    中      |    高    | epoll + multi thread
	| Jmeter      | 低      |   Y    |   Y    |    高      |    高    | multi thread
	| LoadRunner  | 高      |   Y    |   Y    |    高      |    高    | multi thread

	**Jmeter**：并发架构是多线程模拟用户请求，并发量受多线程切换的影响，在并发上百后达到瓶颈，不太适合高并发请求场景。

	**LoadRunner**：利用Controller控制Generator负载生成器产生负载(每台最高大约提供几百的并发请求)，并发架构为多线程，如果模拟高并发情形则需要多台Generator，不适宜简单的内部使用。

	**ab**：操作简便易用，报表丰富；基于epoll的并发架构，并发量非常高；但是可扩展性低，不适合并发请求不同的url；启动后为单线程，cpu利用不够充分

	**siege**：并发架构为多线程，并发量过大时，线程间切换会影响并发性能

	其他：很多极简软件，https或post请求都不支持的就不提及了

	**wrk**：基于epoll的并发架构，并发量非常高；可支持多线程，充分利用了CPU；支持lua的脚本扩展（可以衍生出多种测试方案）；唯一的不足是提供的测试结果不是很丰富。

	**综合比较来看，wrk压力测试工具具备高并发、高扩展等特点，最终我们选定了它作为我们二次开发的原型软件，并将开发重点偏向于压测方案的改进以及性能的可视化展示。**

## visual-wrk的改进点

	1. 降低多线程加载测试配置导致的误差延时。
	2. 支持批量URL的随机压测。
	3. 支持不同权重比的URL文件混合压测。
	4. 丰富测试结果，生成可视化报表，并支持在Jenkins HTML report浏览。
	5. 支持在测试报告中展示被测机器的性能变化（需安在被测机器上安装性能采集模块）

## 下载与安装
	```
	git clone git@github.com:NavInfo2/visual-wrk.git
	cd visual-wrk/
	make
	make install
	```

## 命令行参数
	```
	Usage: wrk <options> <url>
	Options:
	-c, --connections <N>  Connections to keep open //并发个数
	-d, --duration    <T>  Duration of test         //测试时长
	-i, --interval    <T>  Request sampling interval//RPS的采样间隔
	-t, --threads     <N>  Number of threads to use //开启线程数，默认每增加500个并发，增加1个线程，直到线程数与机器核数一致

	-s, --script      <S>  Load Lua script file     //指定lua脚本，当软件不满足用户使用条件，用户可以自定义lua脚本做些特殊处理（默认不使用）
	-j, --json        <S>  Load json data for script//包含http(PATH,METHOD,BODY等类型)的json数据，由lua脚本加载处理后提供给visual-wrk压测主程序使用
	-H, --header      <H>  Add header to request    //为request url增加特殊的header
	--latency          Print latency statistics //统计事物时延
	-v, --version          Print version details    //打印软件版本

	Numeric arguments may include a SI unit (1k, 1M, 1G)
	Time arguments may include a time unit (2s, 2m, 2h)
	```
## 使用方法
	支持的测试方法包括**单一url压测**、**单文件随机压测**以及**多文件权重压测**。
	支持的HTTP方法为**GET**或**POST**。
	支持的测试数据格式为**字符串**或**base64**。

	* 对 http://127.0.0.1:8080/index.html 单一url进行压测
	**单一url压测**：`wrk -t4 -c400 -d30s http://127.0.0.1:8080/index.html --latency`

	*  对data.json中的全部url进行批量的随机压测
	**单文件随机压测**：`wrk -t4 -c400 -d30s -j data.json --latency`

	*  对mixed_test.json中多个文件以按权重比比例随机压测
	**多文件权重压测**：`wrk -t4 -c400 -d30s -j data/mixed_test.json --latency`

	测试完毕后，测试报告位于测试目录下的report/log.html中

	**注:**

	* **单文件随机压测**的文件格式
	```
	{
		"request":
		[
			{
				"path": "url path",
				"method": "GET or POST"
				"bodyType": "base64"
				"body": "body content"
			}
		]
	}
	
	path字段：填充url的path
	method字段：填充POST或GET，其他方法暂未支持
	body字段：method字段填充POST时，该字段为POST的内容；当填充GET时，不需要该字段
	bodyType字段：当body内容为base64格式的数据时，需要增加该字段，并填充base64
	```
	* **多文件权重压测**的文件格式

	```
	{
		"mixed_test":
		[
			{
				"label":"get",
				"file": "data/method_get.json",
				"weight": "1"
			}
		]
	}
	label字段：自定义唯一标签名
	file字段：测试文件路径（以data的上一级目录为基准）
	weight字段：每个文件的测试权重比
	
	当json文件为混合压测文件时，请以“mixed_”开头重命名文件
	```
	
	具体格式内容案例可参见以下文件
	```
	GET请求的文件：data/method_get.json
	POST请求的文件：data/method_post_base64.json
	基于Base64的POST请求的文件：data/method_post.json
	混合压测的文件：data/mixed_test.json
	```


## 压测报告介绍
报告分为三个模块：测试参数、测试结果、服务信息

### 测试参数模块
![在这里插入图片描述](https://img-blog.csdnimg.cn/20190228185839174.png)
上图内容：
* 测试文件为route100_500km.json，用户可以点击进入查看该测试文件。
* URL的地址为https://navicore.mapbar.com，使用了https协议，主机为navicore.mapbar.com，path存在json文件中。
* 此次压测并发量为60，持续测试5分钟。

### 测试结果模块
![在这里插入图片描述](https://img-blog.csdnimg.cn/20190228190202684.png)
上图内容：
	* 在5分钟内发起了7508次请求，接收了858.80MB的数据
* 在传输层发生了一次连接错误，4次read错误(read错误可能是在读取数据时超时导致的)
	* 完成了7058次请求
	* 非2xx或3xx的请求响应个数为5个
	* 平均RPS为每秒25.03个
	* 平均数据传输速率为每秒2.86MB
	* 发现了5次404的HTTP请求错误码

	![在这里插入图片描述](https://img-blog.csdnimg.cn/20190228190455318.png)
	上图内容：
	* 完成HTTP事务时延分布：50%在2.31秒以内，100%的请求在3.59秒以内

	![在这里插入图片描述](https://img-blog.csdnimg.cn/20190228190629448.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3QxMDA1NDYwNzU5,size_16,color_FFFFFF,t_70)
	上图绘制了测试期间，每秒完成的请求个数与response code为200的个数

	![在这里插入图片描述](https://img-blog.csdnimg.cn/20190228190846187.png)
	上表是对测试结果整理，stdev为标准差，+/-stdev为包含一个正负标准差的分布比例

### 服务器信息模块（需在服务器安装信息采集模块）
	该模块收集了被测服务器的硬件信息
	信息采集模块安装及使用方法，可参见(https://blog.csdn.net/t1005460759/article/details/88175107)
	![在这里插入图片描述](https://img-blog.csdnimg.cn/20190228191208379.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3QxMDA1NDYwNzU5,size_16,color_FFFFFF,t_70)
	上图分别为系统信息、CPU信息及硬盘使用率
	![在这里插入图片描述](https://img-blog.csdnimg.cn/2019022819102115.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3QxMDA1NDYwNzU5,size_16,color_FFFFFF,t_70)
	上图为CPU使用率折线图

	![在这里插入图片描述](https://img-blog.csdnimg.cn/20190228191559218.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3QxMDA1NDYwNzU5,size_16,color_FFFFFF,t_70)
	上图为内存使用率折线图
	![在这里插入图片描述](https://img-blog.csdnimg.cn/20190228191623287.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3QxMDA1NDYwNzU5,size_16,color_FFFFFF,t_70)
	上图为IO读写的折线图`
