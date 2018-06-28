#!/usr/bin/env groovy

pipeline {
    agent { label 'linux' }
    parameters {
        choice (
            choices: "data/grid.json\ndata/poi.json\ndata/route/route0_3km.json\ndata/route/route100_500km.json\ndata/route/route10_50km.json\ndata/route/route1k_2kkm.json\ndata/route/route2kkm.json\ndata/route/route3_10km.json\ndata/route/route500_1kkm.json\ndata/route/route50_100km.json\ndata/ti_enroute_v1.json\ndata/ti_enroute_v2.json\ndata/ntd.json",
            description: 'The test case file used for testing',
            name : 'case_file')

        string (
            defaultValue: '60',
            description: 'Number of Concurrent Connections',
            name : 'concurrency')

        string (
            defaultValue: '5m',
            description: 'Duration of test, time arguments may include a time unit (2s, 2m, 2h)',
            name : 'duration')

        string (
            defaultValue: '2',
            description: 'RPS Chart sample interval',
            name : 'sample_interval')

    }
    stages {
        stage('Build') {
            steps {
                echo 'make...'
                sh('make')
            }
        }
        stage('Test') { 
            steps {
                echo 'runing...'
                sh('./wrk -i${sample_interval} -c${concurrency} -d${duration} -s scripts/multi-request-json.lua -j ${case_file} -l report/log.html --latency')
            }
        }
        stage('Publish') {
            steps {
                sh('if [ ! -d "report/data" ]; then cd report && ln -s ../data/ data && cd -; fi;')
                publishHTML([allowMissing: true, 
                        alwaysLinkToLastBuild: false, 
                        keepAll: true, 
                        reportDir: 'report', 
                        reportFiles: 'log.html', 
                        reportName: 'HTML Report', 
                        reportTitles: ''])
            }
        }
    }
}

