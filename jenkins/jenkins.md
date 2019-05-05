# 基于visual-wrk的Jenkins CI压测解决方案

## 压测平台及被测平台的软件部署步骤

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
3. 将压测文件名称加入 data/Jenkinsfile 的 "pipeline--parameters--choice--choices" 字段中，以'\n'分割

#### 通过Jenkins进行压力测试
1. 在 Jenkins 上配置 load-test 参数并执行 build 即可开始一次压力测试

## 如需了解更多
1. [visual-wrk](https://github.com/NavInfoNC/visual-wrk)
2. [system-monitor](https://github.com/NavInfoNC/system-monitor)
