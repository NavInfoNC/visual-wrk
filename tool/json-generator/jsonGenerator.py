#!/usr/bin/python3
# coding: utf-8
import sys
import os
import json
import base64
import argparse

class jsonGenerator:
    def __init__(self):
        self.stressDict = {}
        self.requestList = []

    def createRequest(self, path, body, method, bodyType):
        request = {}
        request.setdefault("path", path)
        if body != None:
            request.setdefault("body", body)
        request.setdefault("method", method)
        if bodyType != None:
            request.setdefault("bodyType", bodyType)
        self.requestList.append(request)

    def stressJson(self):
        if len(self.requestList) == 0:
            return None
        self.stressDict.setdefault("request", self.requestList)
        stressDict = self.stressDict
        return json.dumps(stressDict, ensure_ascii=False, sort_keys=False, indent=4, separators=(',', ':'))

    def jsonByBase64Dir(self, base64Path, urlsFile):
        if not os.path.isdir(base64Path):
            return None

        f = open(urlsFile, 'r')
        urlsFileLines = f.readlines()
        urlsFileLinesNum = len(urlsFileLines)
        f.close()

        files= os.listdir(base64Path)
        fileNum = len(files)
        if urlsFileLinesNum != fileNum and urlsFileLinesNum != 1:
            return

        i = 0
        for file in files:
            url = urlsFileLines[i].strip('\n')
            filePath = base64Path + "/" + file
            if os.path.isfile(filePath):
                with open(filePath, 'rb') as contentFile:
                    content = contentFile.read()
                    base64Content = base64.b64encode(content).decode("utf-8")
                    self.createRequest(url, base64Content, "POST", "base64")
                    if urlsFileLinesNum != 1:
                        i += 1

        if urlsFileLinesNum != i and urlsFileLinesNum != 1:
            return

        return self.stressJson()

    def jsonByPostFile(self, file, urlsFile):
        if not os.path.isfile(file) or not os.path.isfile(urlsFile) :
            return None
        
        f = open(urlsFile, 'r')
        urlsFileLines = f.readlines()
        urlsFileLinesNum = len(urlsFileLines)
        f.close()

        f = open(file, 'r')
        bodyLines = f.readlines()
        bodyLinesNum = len(bodyLines)
        if urlsFileLinesNum != bodyLinesNum and urlsFileLinesNum != 1:
            f.close()
            return

        i = 0
        for line in bodyLines:
            url = urlsFileLines[i].strip('\n')
            self.createRequest(url, line.strip('\n'), "POST", None)
            if urlsFileLinesNum != 1:
                i += 1
        f.close()

        return self.stressJson()

    def jsonByGetFile(self, filePath):
        if not os.path.isfile(filePath):
            return None
        f = open(filePath)
        for requestPath in f:
            self.createRequest(requestPath.strip('\n'), None, "GET", None)
        f.close()
        return self.stressJson()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Visual-Wrk Json Generator.')
    parser.add_argument('-t', '--type', type=str, choices=['GET', 'POST_char', 'POST_bin'], help='Constructed Type', required=True)
    parser.add_argument('-b', '--bodiesFile', type=str, help='Enter post bodies file path', required=True)
    parser.add_argument('-u', '--urlsFile', type=str, help='Enter URLs file path', required=True)
    parser.add_argument('-o', '--output', type=str, help='Enter output json file path', required=True)
    args = parser.parse_args()
    type = args.__dict__['type']
    bodiesFile = args.__dict__['bodiesFile']
    urlsFile = args.__dict__['urlsFile']
    output = args.__dict__['output']

    creater = jsonGenerator()
    jsonContent = None
    if type == 'GET':
        jsonContent = creater.jsonByGetFile(urlsFile)
    elif type == 'POST_char':
        jsonContent = creater.jsonByPostFile(bodiesFile, urlsFile)
    elif type == 'POST_bin':
        jsonContent = creater.jsonByBase64Dir(bodiesFile, urlsFile)
    else:
        print("Unknown type(%s)" %type)
        exit(1)

    if jsonContent == None:
        print("Json Content is NULL")
        exit(1)

    print(jsonContent)

    fp = open(output, 'w')
    fp.write(jsonContent)
    fp.close()
