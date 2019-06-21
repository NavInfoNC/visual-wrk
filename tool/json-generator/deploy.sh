#!/bin/bash

dst=/etc/ncserver/wrk-json-generator
mkdir -p $dst
mkdir -p $dst/data
rsync -avP *.py web $dst
