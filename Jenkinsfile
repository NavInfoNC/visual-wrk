#!/usr/bin/env groovy

pipeline {
    agent { label 'linux' }
    stages {
        stage('Build') {
            steps {
                echo 'make...'
                sh('make')
            }
        }
    }
}

