.PHONY: build
build:
	rm -rf build dist mkninja.egg-info
	MKNINJA_BUILD_SDIST=1 python -m build -s
	python -m build -w

clean:
	rm -rf build dist mkninja.egg-info findglob/{findglob,test} manifest/manifest
