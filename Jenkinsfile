#!/usr/bin/env groovy

pipeline {
    agent { label 'linux' }
    parameters {
        string (
            defaultValue: '2',
            description: 'Requests sampling interval',
            name : 'sample_interval')

        string (
            defaultValue: '60',
            description: 'Connections to keep open',
            name : 'concurrency')

        string (
            defaultValue: '5M',
            description: 'Duration of test, time arguments may include a time unit (2s, 2m, 2h)',
            name : 'duration')

        choice (
                choices: 'data/grid.json\
                data/poi.json\
                data/route/route0_3km.json\
                data/route/route100_500km.json\
                data/route/route10_50km.json\
                data/route/route1k_2kkm.json\
                data/route/route2kkm.json\
                data/route/route3_10km.json\
                data/route/route500_1kkm.json\
                data/route/route50_100km.json\
                data/ti_enroute_v1.json\
                data/ti_enroute_v2.json'
            description: 'Load json data for script',
            name : 'json_file')
    }
    environment {
        buildParam = 'buildParam1'
        build1Param = 'buildParam2'
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
                sh('./wrk -i${sample_interval} -c${concurrency} -d${duration} -s scripts/multi-request-json.lua -j ${json_file} -l report/log.html --latency')
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

