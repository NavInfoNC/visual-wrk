#!/usr/bin/python3
from flup.server.fcgi import WSGIServer
from threading import Thread
from cgi import parse_qs
import logging.handlers
import logging
import threading
import platform
import patoolib
import json
import time
import sys
import os
import shutil
import hashlib
from jsonGenerator import jsonGenerator

g_original_dir = "data/original"
g_json_dir = "data/json"

def md5(str):
    m = hashlib.md5()   
    m.update(str.encode("utf-8"))
    return m.hexdigest()

def webapp(environ, start_response):
    method = environ['REQUEST_METHOD']
    response = {}
    response['resultCode'] = 0
    request_url = environ['REQUEST_URI']
    result_msg = time.asctime(time.localtime(time.time())) + request_url + " " + method + ":"
    if method == 'PUT' and request_url.startswith("/wrk_json/upload", 0):
        file_name = request_url.split("/")[-1]
        if file_name:
            body = environ['wsgi.input'].read()
            str1 = str(time.time()) + file_name
            md5_string = md5(str1)
            src_path = g_original_dir + "/" + md5_string
            fp = open(src_path, "wb")
            try:
                fp.write(body)
                response['resultCode'] = 1
                response['md5'] = md5_string
                result_msg = result_msg + "Upload success"
            except Exception as e:
                result_msg = result_msg + type(e) + str(e)
            fp.close()
        else:
            result_msg = result_msg + "Without file name(" + file_name + ") in url"
    elif method == 'GET':
        query = parse_qs(environ['QUERY_STRING'])
        if query and request_url.startswith("/wrk_json/convert"):
            md5_string = query.get('md5')
            url_path = query.get('url_path')
            convert_type = query.get('convert_type')
            src_path = g_original_dir + "/" + md5_string[0]
            if md5_string == None or convert_type == None:
                result_msg = result_msg + "Query string incomplete!"
            elif os.path.exists(src_path) == False:
                result_msg = result_msg + "Convert file(" + src_path + ") doesn't exist!"
            else:
                creater = jsonGenerator()
                json_content = []
                if convert_type[0] == 'convert-get':
                    json_content = creater.jsonByGetFile(src_path)
                elif convert_type[0] == 'convert-post-char' and url_path != None:
                    json_content = creater.jsonByPostFile(src_path, url_path[0])
                elif convert_type[0] == 'convert-post-bin' and url_path != None:
                    rar_path = src_path
                    rar_dir = os.path.dirname(rar_path)
                    try:
                        unpacked_dir = rar_dir + "/_" + md5_string[0]
                        if os.path.exists(unpacked_dir) == False:
                            os.mkdir(unpacked_dir)
                            patoolib.extract_archive(rar_path, outdir=unpacked_dir)
                        json_content = creater.jsonByBase64Dir(unpacked_dir, url_path[0])
                    except Exception as e:
                        result_msg = result_msg + type(e) + str(e)
                else:
                    result_msg = result_msg + "Conversion type is incorrent!"

                if json_content != None:
                    dst_path = g_json_dir + "/" + md5_string[0] + ".json"
                    json_fp = open(dst_path, "w")
                    try:
                        json_fp.write(json_content)
                        response['convertedFile'] = md5_string[0] + ".json"
                        response['resultCode'] = 1
                        result_msg = result_msg + "Convert success"
                    except Exception as e:
                        result_msg = result_msg + type(e) + str(e)
                    json_fp.close()
                else:
                    result_msg = result_msg + "Conversion failed, please check source file"
        else:
            result_msg = result_msg + "Unknown url(" + request_url + ")"

    g_logger.info(result_msg)
    response["resultMsg"] = result_msg
    start_response('200 OK', [('Content-Type', 'application/json')])
    content = json.dumps(response, sort_keys=True, indent=4, separators=(',', ':'))
    return content

class serverThread(threading.Thread):
    def __init__(self, name, loop, stop):
        threading.Thread.__init__(self)
        self.name = name
        self.loop = loop
        self.stop = stop

    def run(self):
        self.loop()

    def stop(self):
        self.stop()

LOG_FILE = 'jsonGenerator.log'

def removeDir(top):
    for root, dirs, files in os.walk(top, topdown=False):
        for name in files:
            os.remove(os.path.join(root, name))
        for name in dirs:
            os.rmdir(os.path.join(root, name))

log_handler = logging.handlers.RotatingFileHandler(LOG_FILE, maxBytes = 1024*1024, backupCount = 5) 
g_logger = logging.getLogger('jsonGenerator')
g_logger.addHandler(log_handler)
g_logger.setLevel(logging.DEBUG)

if __name__ == '__main__':
    print(platform.python_version())
    if os.path.isdir(g_json_dir) == False:
        os.makedirs(g_json_dir, mode=0o777)
    if os.path.isdir(g_original_dir) == False:
        os.makedirs(g_original_dir, mode=0o777)
    WSGIServer(webapp, bindAddress='/etc/ncserver/wrk-json-generator/.ncserver.sock', umask=0000).run()
