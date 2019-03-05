json generator
---
为visual wrk生成json格式的测试文件

## 环境要求

Linux系统中需安装以下软件

```
sudo apt-get install python3
pip3 install base64
pip3 install json
```

## 命令行参数

```
-t --type Constructed Type //构造json的HTTP method类型，
				//GET：GET请求；
				//POST_char：body内容为字符串的POST请求；
				//POST_bin：body内容为BASE64格式的POST请求，通常对二进制数据进行BASE64的格式转换

-b --bodiesFile //type为POST_char时:bodiesFile文件内容由多个POST body构成，文件中每行数据对应一个body；
		//type为POST_bin时:bodiesFile文件是rar/zip压缩包,包内容为多个文件，每个文件对应一个body


-u --urlsFile //指定包含多个url的文件，每行对应一个url，url中不包含<scheme>://<host>:<port>
		//如果文件中只有一个url，将一对多批量对应POST body文件
		//如果文件中有多个url，数量必须与POST body的个数一致，否则转换失败

-o --output //指定输出文件
```

## 使用方法

```
//案例一：构造POST body为字符流的POST请求
python3 jsonGenerator.py -t POST_char -b test/post_char.txt  -u test/url.txt -o a.json

//案例二：构造GET请求
python3 jsonGenerator.py -t GET -b test/get.txt  -u test/url.txt -o a.json

//案例三：构造POST body为二进制转base64后的POST请求
python3 jsonGenerator.py -t POST_bin -b test/post_bin.rar  -u test/url.txt -o a.json
```

