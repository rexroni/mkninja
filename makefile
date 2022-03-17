.PHONY: build
build:
	rm -rf build dist mkninja.egg-info
	python -m build

clean:
	rm -rf build dist mkninja.egg-info findglob/{findglob,test} manifest/manifest mkninja/{manifest,findglob}
