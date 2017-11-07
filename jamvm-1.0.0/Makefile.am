SUBDIRS = src
bin_SCRIPTS = jamvm

jamvm: Makefile
	echo '#! /bin/sh' > $@
	echo 'export CLASSPATH=${with_classpath_install_dir}/share/classpath:.:$$CLASSPATH' >> $@
	echo 'export LD_LIBRARY_PATH=${with_classpath_install_dir}/lib/classpath:.:$$LD_LIBRARY_PATH' >> $@
	echo 'export PATH=${libexecdir}:$$PATH' >> $@
	echo $@' $$@' >> $@
