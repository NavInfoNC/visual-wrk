# 基于visual-wrk的Jenkins CI压测解决方案

## 压测平台及被测平台的软件部署步骤

#### visual-wrk的下载与安装(压测平台)
1. [安装步骤](https://github.com/NavInfoNC/visual-wrk/blob/master/docs/visual-wrk-blog.md#3-%E4%B8%8B%E8%BD%BD%E4%B8%8E%E5%AE%89%E8%A3%85)

#### visual-wrk与Jenkins集成(压测平台)
1. 构建load-test目录
```
mkdir ~/load-test
mkdir ~/load-test/data
cd visual-wrk/
cp Jenkinsfile template/ ~/load-test/
```
2. 将load-test目录做成git仓库
3. 在Jenkins上安装HTML Publisher plugin插件
4. 在Jenkins上创建load-test的Pipeline,并与load-test仓库关联

#### system-monitor部署(被测平台)
1. [安装步骤](https://github.com/NavInfoNC/system-monitor/blob/master/README.rst)

#### 创建压力测试文件
1. 创建压力测试文件![步骤](https://github.com/NavInfoNC/visual-wrk/blob/master/docs/visual-wrk-blog.md#5-%E4%BD%BF%E7%94%A8%E6%96%B9%E6%B3%95)
2. 将压测文件PUSH至load-test仓库的data目录下

#### 通过Jenkins进行压力测试
1. 在Jenkins上配置load-test参数并执行build即可开始一次压力测试

## 如需了解更多
1. [visual-wrk](https://github.com/NavInfoNC/visual-wrk)
2. [system-monitor](https://github.com/NavInfoNC/system-monitor)
