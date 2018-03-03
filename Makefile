
.PHONY: build
build:
	arduino --verbose --verify linak_control.ino

.PHONY: upload
upload:
	arduino --verbose --upload --port /dev/ttyACM0 linak_control.ino
