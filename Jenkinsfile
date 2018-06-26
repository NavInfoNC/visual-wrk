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
            choices: 'data/grid.json',
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

