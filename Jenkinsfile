pipeline{
	agent none
		stages {
			stage ('Early checks') {
				agent { node { label 'master' } }
				stages {
					stage ('Check for RFC/WIP builds') {
						when {
							changeRequest comparator: 'REGEXP', title: '.*(rfc|RFC|wip|WIP).*'
								beforeAgent true
						}
						steps {
							error("Failing as this is marked as a WIP or RFC PR.")
						}
					}
					stage ('Cancel older builds') {
						when { not { branch 'master' } }
						steps {
							cancelPreviousBuilds()
						}
					}
				}
			}
			stage ('Worker build') {
				agent { node { label 'groovy' } }
				stages {
					stage ('Checkout') {
						steps {
							checkout scm
						}
					}
					stage ('Install dependencies') {
						steps {
							sh "sudo apt update && sudo apt install -y meson ninja-build gcc libxml2-utils xsltproc python3-docutils libglib2.0-dev libgnutls28-dev libxml2-dev libnl-3-dev libnl-route-3-dev libyajl-dev make libcurl4-gnutls-dev qemu-utils libssl-dev mtools libudev-dev libpciaccess-dev"
						}
					}
					stage ('Configure') {
						steps {
							sh "meson build -D driver_ch=enabled -D driver_qemu=disabled -D driver_openvz=disabled -D driver_esx=disabled -D driver_vmware=disabled -D driver_lxc=disabled -D driver_libxl=disabled -D driver_vbox=disabled -D selinux=disabled -D system=true --prefix=/usr"
						}
					}
					stage ('Build & Install') {
						steps {
							sh "ninja -C build"
							sh "sudo ninja -C build install"
                                                        sh "sudo rm -fr /etc/libvirt/qemu/networks/*"
							sh "sudo ldconfig"
						}
					}
					stage ('Install Rust') {
						steps {
							sh "curl https://sh.rustup.rs -sSf | sh -s -- -y"
						}
					}
					stage ('Build & Install Cloud Hypervisor') {
						steps {
							sh "ch_integration_tests/build_ch.sh"
						}
					}
					stage ('Run integration tests') {
						steps {
							sh "ch_integration_tests/run_tests.sh"
						}
					}

				}

			}
		}
}

def cancelPreviousBuilds() {
	// Check for other instances of this particular build, cancel any that are older than the current one
	def jobName = env.JOB_NAME
		def currentBuildNumber = env.BUILD_NUMBER.toInteger()
		def currentJob = Jenkins.instance.getItemByFullName(jobName)

		// Loop through all instances of this particular job/branch
		for (def build : currentJob.builds) {
			if (build.isBuilding() && (build.number.toInteger() < currentBuildNumber)) {
				echo "Older build still queued. Sending kill signal to build number: ${build.number}"
					build.doStop()
			}
		}
}
