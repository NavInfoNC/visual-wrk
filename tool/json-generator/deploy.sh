#!/bin/bash

dst=/etc/ncserver/wrk-json-generator
mkdir -p $dst
rsync -avP *.py web $dst
