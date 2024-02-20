pipeline {
  agent none

  triggers {
    pollSCM('H 21 * * 1,2,4,5,7')
  }

  environment {
    OUTPUT_DIR = 'packages'
    TEST_REPORT = 'reports'
    JUNIT_REQUIRED = true
  }

  stages {
    stage('Build and Test') {
      parallel {
        stage('Linux Release') {
          agent {
            docker {
              image 'cubridci/cubridci:develop'
              label 'linux'
              alwaysPull true
            }
          }
          environment {
            MAKEFLAGS = '-j'
          }
          steps {
            script {
              currentBuild.displayName = sh(returnStdout: true, script: './build.sh -v').trim()
            }

            echo 'Building...'
//            sh 'scl enable devtoolset-8 -- /entrypoint.sh build'

            echo 'Packing...'
//            sh "scl enable devtoolset-8 -- /entrypoint.sh dist -o ${OUTPUT_DIR}"

<<<<<<< HEAD
            // echo 'Testing...'
            // sh '/entrypoint.sh test || echo "$? failed"'
            script {
              // Skip testing for feature branches
              if (!(env.BRANCH_NAME ==~ /^feature\/.*/)) {
            	echo 'Testing...'
            	sh '/entrypoint.sh test || echo "$? failed"'
              } else {
                echo "Skipping testing for feature branch"
=======
            script {
              if (env.BRANCH_NAME ==~ /^feature\/.*/) {
                echo 'Skip testing for feature branch'
                JUNIT_REQUIRED = false
              } else {
            	echo 'Testing...'
            	sh '/entrypoint.sh test || echo "$? failed"'
>>>>>>> 3186fa8b4d1eeeec69b23db8e3888074951723c6
              }
            }
          }
          post {
            always {
              archiveArtifacts "${OUTPUT_DIR}/*"
<<<<<<< HEAD
            //  junit "${TEST_REPORT}/*.xml"
=======
              if (env.JUNIT_REQUIRED) {
                junit "${TEST_REPORT}/*.xml"
              }
>>>>>>> 3186fa8b4d1eeeec69b23db8e3888074951723c6
            }
          }
        }

        stage('Linux Debug') {
          agent {
            docker {
              image 'cubridci/cubridci:develop'
              label 'linux'
              alwaysPull true
            }
          }
          environment {
            MAKEFLAGS = '-j'
          }
          steps {
            echo 'Building...'
//            sh 'scl enable devtoolset-8 -- /entrypoint.sh build -m debug'
            
            echo 'Packing...'
//            sh "scl enable devtoolset-8 -- /entrypoint.sh dist -m debug -o ${OUTPUT_DIR}"

<<<<<<< HEAD
            // echo 'Testing...'
            // sh '/entrypoint.sh test || echo "$? failed"'
            script {
              // Skip testing for feature branches
              if (!(env.BRANCH_NAME ==~ /^feature\/.*/)) {
            	echo 'Testing...'
            	sh '/entrypoint.sh test || echo "$? failed"'
              } else {
                echo "Skipping testing for feature branch"
=======
            script {
              if (env.BRANCH_NAME ==~ /^feature\/.*/) {
                echo 'Skip testing for feature branch'
                JUNIT_REQUIRED = false
              } else {
            	echo 'Testing...'
            	sh '/entrypoint.sh test || echo "$? failed"'
>>>>>>> 3186fa8b4d1eeeec69b23db8e3888074951723c6
              }
            }
          }
          post {
            always {
              archiveArtifacts "${OUTPUT_DIR}/*"
<<<<<<< HEAD
              //junit "${TEST_REPORT}/*.xml"
=======
              if (env.JUNIT_REQUIRED) {
                junit "${TEST_REPORT}/*.xml"
              }
>>>>>>> 3186fa8b4d1eeeec69b23db8e3888074951723c6
            }
          }
        }

        stage('Windows Release') {
          when {
            expression {
              // Skip Windows Release stage for feature branches
              return !(env.BRANCH_NAME ==~ /^feature\/.*/)
            }
          }
          agent {
            node {
              label 'windows'
            }
          }
          steps {
            echo 'Building...'
            bat "win/build.bat build"

            echo 'Packing...'
            bat "win/build.bat /out ${OUTPUT_DIR} dist"
          }
          post {
            always {
              archiveArtifacts "${OUTPUT_DIR}/*"
            }
          }
        }
      }
    }
  }

  post {
    always {
<<<<<<< HEAD
//      build job: "${DEPLOY_JOB}", parameters: [string(name: 'PROJECT_NAME', value: "${JOB_NAME}")],
      build job: "${DEPLOY_JOB_FOR_MANUAL}", parameters: [string(name: 'PROJECT_NAME', value: "${JOB_NAME}")],
            propagate: false
//      emailext replyTo: '$DEFAULT_REPLYTO', to: '$DEFAULT_RECIPIENTS',
      emailext replyTo: '$DEFAULT_REPLYTO', to: 'twkang@cubrid.com',
=======
      script {
        if (env.BRANCH_NAME ==~ /^feature\/.*/) {
          build job: "${DEPLOY_JOB_FOR_MANUAL}", parameters: [string(name: 'PROJECT_NAME', value: "${JOB_NAME}")],
                propagate: false
        } else {
          build job: "${DEPLOY_JOB}", parameters: [string(name: 'PROJECT_NAME', value: "${JOB_NAME}")],
                propagate: false
        }
      }
      emailext replyTo: '$DEFAULT_REPLYTO', to: '$DEFAULT_RECIPIENTS',
>>>>>>> 3186fa8b4d1eeeec69b23db8e3888074951723c6
               subject: '$DEFAULT_SUBJECT', body: '''${JELLY_SCRIPT,template="html"}'''
    }
  }
}
