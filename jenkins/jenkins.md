# 基于visual-wrk的Jenkins CI压测解决方案

## 压测平台及被测平台的软件部署步骤

#### 安装 Jenkins

```
参考网址
https://linuxize.com/post/how-to-install-jenkins-on-ubuntu-18-04/
https://wiki.jenkins.io/display/JENKINS/Installing+Jenkins+on+Ubuntu
https://www.cnblogs.com/sparkdev/p/7102622.html

master 机器配置

	sudo apt install -y openjdk-8-jdk git
	wget -q -O - https://pkg.jenkins.io/debian/jenkins-ci.org.key | sudo apt-key add -
	sudo sh -c 'echo deb http://pkg.jenkins.io/debian-stable binary/ > /etc/apt/sources.list.d/jenkins.list'
	sudo apt-get update
	sudo apt-get install -y jenkins

	将下面这句话附到 /etc/default/jenkins 中
		JAVA_ARGS="-Djava.awt.headless=true -Dhudson.model.DirectoryBrowserSupport.CSP=\"img-src 'self' https://www.amcharts.com; style-src 'self' 'unsafe-inline'; child-src 'self'; frame-src 'self' ; llow-scripts script-src 'self' 'unsafe-inline' 'unsafe-eval'\""

	sudo service jenkins start

	安装 git、github、gitlab、blue ocean、ldap 插件(如果插件安装失败请多试几次，或者上网搜索解决方法)

	配置 ldap

	设置 Jenkins->Manage Jenkins->Configure Global Security
	勾选 Enable security ---- 选择 Random

	Jenkins->Manage Jenkins->Manage Nodes 添加 slave


slave 机器安装

	sudo apt install -y openjdk-8-jdk git
	下载 agent
	启动

```

#### visual-wrk的下载与安装(压测平台)
1. [安装步骤](https://github.com/NavInfoNC/visual-wrk/blob/master/docs/visual-wrk-blog.md#3-%E4%B8%8B%E8%BD%BD%E4%B8%8E%E5%AE%89%E8%A3%85)

#### 准备 json 文件
[准备 json 文件](../tool/json-generator/README.md)

#### visual-wrk与Jenkins集成(压测平台)
1. 构建 load-test 目录

	```
	mkdir ~/load-test
	cd visual-wrk/
	cp jenkins/Jenkinsfile data/ ~/load-test/ -R
	```

2. 将 load-test 目录做成 git 仓库
3. 在 Jenkins 上安装 HTML Publisher plugin 插件
4. 在 Jenkins 上创建 load-test 的 Pipeline, 并与 load-test 仓库关联

#### system-monitor部署(被测平台)
1. [安装步骤](https://github.com/NavInfoNC/system-monitor/blob/master/README.rst)

#### 创建压力测试文件
1. 创建压力测试文件[步骤](https://github.com/NavInfoNC/visual-wrk/blob/master/docs/visual-wrk-blog.md#5-%E4%BD%BF%E7%94%A8%E6%96%B9%E6%B3%95)
2. 将压测文件 PUSH 至 load-test 仓库的 data 目录下
3. 将压测文件名称加入 Jenkinsfile 的 "pipeline--parameters--choice--choices" 字段中，以'\n'分割

#### 通过Jenkins进行压力测试
1. 在 Jenkins 上配置 load-test 参数并执行 build 即可开始一次压力测试

## 如需了解更多
1. [visual-wrk](https://github.com/NavInfoNC/visual-wrk)
2. [system-monitor](https://github.com/NavInfoNC/system-monitor)
