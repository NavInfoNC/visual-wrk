#!/bin/bash
dataPath="data/route"
routeArray=("route0_3km.json" "route3_10km.json" "route10_50km.json" "route50_100km.json" "route100_500km.json" "route500_1kkm.json" "route1k_2kkm.json" "route2kkm.json" "route2kkm+.json")
reportFile="log.rst"
sampleInterval=30
threadNum=4
concurrency=60
duration="6s"
url="https://navicore.mapbar.com"

if [ -f "$reportFile" ]; then 
    rm ${reportFile}
    echo "rm ${reportFile}"
fi 

idx=1
for route in ${routeArray[@]}  
do  
    echo "run ${route}"
    echo "" >> ${reportFile}
    echo "${idx}. TEST CASE" >> ${reportFile}
    echo "=================" >> ${reportFile}
    ./wrk  -i${sampleInterval} -t${threadNum} -c${concurrency} -d${duration} -l ${reportFile} -s scripts/multi-request-json.lua -j ${dataPath}/${route} ${url} --latency --timeout=100
    let idx++
done  
