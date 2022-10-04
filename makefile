.PHONY: build
build:
	rm -rf build dist mkninja.egg-info
	python -m build

install_local:
	make -C findglob && mv findglob/findglob mkninja
	make -C manifest && mv manifest/manifest mkninja
	pip install -e .

clean:
	rm -rf build dist mkninja.egg-info findglob/{findglob,test} manifest/manifest mkninja/{manifest,findglob}
