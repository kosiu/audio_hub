#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>

close(self->fd) == -1);

SpiDev_writebytes(SpiDevObject *self, PyObject *args){
	status = write(self->fd, &buf[0], len);
}

SpiDev_readbytes(SpiDevObject *self, PyObject *args){
	status = read(self->fd, &rxbuf[0], len);
}
xfer(){
	status = ioctl(self->fd, SPI_IOC_MESSAGE(len), xferptr);
	status = ioctl(self->fd, SPI_IOC_MESSAGE(1), &xfer);
	if (self->mode & SPI_CS_HIGH) status = read(self->fd, &rxbuf[0], 0);
}


SpiDev_xfer2(SpiDevObject *self, PyObject *args){
	status = ioctl(self->fd, SPI_IOC_MESSAGE(1), &xfer);
	if (self->mode & SPI_CS_HIGH) status = read(self->fd, &rxbuf[0], 0);
}

SpiDev_xfer3(SpiDevObject *self, PyObject *args){
	status = ioctl(self->fd, SPI_IOC_MESSAGE(2), &xfer);
	if (self->mode & SPI_CS_HIGH) status = read(self->fd, &rxbuf[0], 0);
}

static int __spidev_set_mode( int fd, __u8 mode) {
	ioctl(fd, SPI_IOC_WR_MODE, &mode);
	ioctl(fd, SPI_IOC_RD_MODE, &test);
}

PyDoc_STRVAR(SpiDev_fileno_doc,
	"fileno() -> integer \"file descriptor\"\n\n"
	"This is needed for lower-level file interfaces, such as os.read().\n");

static PyObject *
SpiDev_fileno(SpiDevObject *self){
	PyObject *result = Py_BuildValue("i", self->fd);
	Py_INCREF(result);
	return result;
}

static PyObject *
SpiDev_get_mode(SpiDevObject *self, void *closure){
	PyObject *result = Py_BuildValue("i", (self->mode & (SPI_CPHA | SPI_CPOL) ) );
	Py_INCREF(result);
	return result;
}

static PyObject *
SpiDev_get_cshigh(SpiDevObject *self, void *closure)
{
	PyObject *result;

	if (self->mode & SPI_CS_HIGH)
		result = Py_True;
	else
		result = Py_False;

	Py_INCREF(result);
	return result;
}

static PyObject *
SpiDev_get_lsbfirst(SpiDevObject *self, void *closure)
{
	PyObject *result;

	if (self->mode & SPI_LSB_FIRST)
		result = Py_True;
	else
		result = Py_False;

	Py_INCREF(result);
	return result;
}

static PyObject *
SpiDev_get_3wire(SpiDevObject *self, void *closure)
{
	PyObject *result;

	if (self->mode & SPI_3WIRE)
		result = Py_True;
	else
		result = Py_False;

	Py_INCREF(result);
	return result;
}

static PyObject *
SpiDev_get_loop(SpiDevObject *self, void *closure)
{
	PyObject *result;

	if (self->mode & SPI_LOOP)
		result = Py_True;
	else
		result = Py_False;

	Py_INCREF(result);
	return result;
}

static PyObject *
SpiDev_get_no_cs(SpiDevObject *self, void *closure)
{
        PyObject *result;

        if (self->mode & SPI_NO_CS)
                result = Py_True;
        else
                result = Py_False;

        Py_INCREF(result);
        return result;
}


static int
SpiDev_set_mode(SpiDevObject *self, PyObject *val, void *closure)
{
	uint8_t mode, tmp;

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError,
			"Cannot delete attribute");
		return -1;
	}
#if PY_MAJOR_VERSION < 3
	if (PyInt_Check(val)) {
		mode = PyInt_AS_LONG(val);
	} else
#endif
	{
		if (PyLong_Check(val)) {
			mode = PyLong_AS_LONG(val);
		} else {
			PyErr_SetString(PyExc_TypeError,
				"The mode attribute must be an integer");
			return -1;
		}
	}


	if ( mode > 3 ) {
		PyErr_SetString(PyExc_TypeError,
			"The mode attribute must be an integer"
				 "between 0 and 3.");
		return -1;
	}

	// clean and set CPHA and CPOL bits
	tmp = ( self->mode & ~(SPI_CPHA | SPI_CPOL) ) | mode ;

	__spidev_set_mode(self->fd, tmp);

	self->mode = tmp;
	return 0;
}

static int
SpiDev_set_cshigh(SpiDevObject *self, PyObject *val, void *closure)
{
	uint8_t tmp;

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError,
			"Cannot delete attribute");
		return -1;
	}
	else if (!PyBool_Check(val)) {
		PyErr_SetString(PyExc_TypeError,
			"The cshigh attribute must be boolean");
		return -1;
	}

	if (val == Py_True)
		tmp = self->mode | SPI_CS_HIGH;
	else
		tmp = self->mode & ~SPI_CS_HIGH;

	__spidev_set_mode(self->fd, tmp);

	self->mode = tmp;
	return 0;
}

static int
SpiDev_set_lsbfirst(SpiDevObject *self, PyObject *val, void *closure)
{
	uint8_t tmp;

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError,
			"Cannot delete attribute");
		return -1;
	}
	else if (!PyBool_Check(val)) {
		PyErr_SetString(PyExc_TypeError,
			"The lsbfirst attribute must be boolean");
		return -1;
	}

	if (val == Py_True)
		tmp = self->mode | SPI_LSB_FIRST;
	else
		tmp = self->mode & ~SPI_LSB_FIRST;

	__spidev_set_mode(self->fd, tmp);

	self->mode = tmp;
	return 0;
}

static int
SpiDev_set_3wire(SpiDevObject *self, PyObject *val, void *closure)
{
	uint8_t tmp;

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError,
			"Cannot delete attribute");
		return -1;
	}
	else if (!PyBool_Check(val)) {
		PyErr_SetString(PyExc_TypeError,
			"The 3wire attribute must be boolean");
		return -1;
	}

	if (val == Py_True)
		tmp = self->mode | SPI_3WIRE;
	else
		tmp = self->mode & ~SPI_3WIRE;

	__spidev_set_mode(self->fd, tmp);

	self->mode = tmp;
	return 0;
}

static int
SpiDev_set_no_cs(SpiDevObject *self, PyObject *val, void *closure)
{
        uint8_t tmp;

        if (val == NULL) {
                PyErr_SetString(PyExc_TypeError,
                        "Cannot delete attribute");
                return -1;
        }
        else if (!PyBool_Check(val)) {
                PyErr_SetString(PyExc_TypeError,
                        "The no_cs attribute must be boolean");
                return -1;
        }

        if (val == Py_True)
                tmp = self->mode | SPI_NO_CS;
        else
                tmp = self->mode & ~SPI_NO_CS;

        __spidev_set_mode(self->fd, tmp);

        self->mode = tmp;
        return 0;
}


static int
SpiDev_set_loop(SpiDevObject *self, PyObject *val, void *closure)
{
	uint8_t tmp;

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError,
			"Cannot delete attribute");
		return -1;
	}
	else if (!PyBool_Check(val)) {
		PyErr_SetString(PyExc_TypeError,
			"The loop attribute must be boolean");
		return -1;
	}

	if (val == Py_True)
		tmp = self->mode | SPI_LOOP;
	else
		tmp = self->mode & ~SPI_LOOP;

	__spidev_set_mode(self->fd, tmp);

	self->mode = tmp;
	return 0;
}

static PyObject *
SpiDev_get_bits_per_word(SpiDevObject *self, void *closure)
{
	PyObject *result = Py_BuildValue("i", self->bits_per_word);
	Py_INCREF(result);
	return result;
}

static int
SpiDev_set_bits_per_word(SpiDevObject *self, PyObject *val, void *closure)
{
	uint8_t bits;

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError,
			"Cannot delete attribute");
		return -1;
	}
#if PY_MAJOR_VERSION < 3
	if (PyInt_Check(val)) {
		bits = PyInt_AS_LONG(val);
	} else
#endif
	{
		if (PyLong_Check(val)) {
			bits = PyLong_AS_LONG(val);
		} else {
			PyErr_SetString(PyExc_TypeError,
				"The bits_per_word attribute must be an integer");
			return -1;
		}
	}

		if (bits < 8 || bits > 16) {
		PyErr_SetString(PyExc_TypeError,
			"invalid bits_per_word (8 to 16)");
		return -1;
	}

	if (self->bits_per_word != bits) {
		if (ioctl(self->fd, SPI_IOC_WR_BITS_PER_WORD, &bits) == -1) {
			PyErr_SetFromErrno(PyExc_IOError);
			return -1;
		}
		self->bits_per_word = bits;
	}
	return 0;
}

static PyObject *
SpiDev_get_max_speed_hz(SpiDevObject *self, void *closure)
{
	PyObject *result = Py_BuildValue("i", self->max_speed_hz);
	Py_INCREF(result);
	return result;
}

static int
SpiDev_set_max_speed_hz(SpiDevObject *self, PyObject *val, void *closure)
{
	uint32_t max_speed_hz;

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError,
			"Cannot delete attribute");
		return -1;
	}
#if PY_MAJOR_VERSION < 3
	if (PyInt_Check(val)) {
		max_speed_hz = PyInt_AS_LONG(val);
	} else
#endif
	{
		if (PyLong_Check(val)) {
			max_speed_hz = PyLong_AS_LONG(val);
		} else {
			PyErr_SetString(PyExc_TypeError,
				"The max_speed_hz attribute must be an integer");
			return -1;
		}
	}

	if (self->max_speed_hz != max_speed_hz) {
		if (ioctl(self->fd, SPI_IOC_WR_MAX_SPEED_HZ, &max_speed_hz) == -1) {
			PyErr_SetFromErrno(PyExc_IOError);
			return -1;
		}
		self->max_speed_hz = max_speed_hz;
	}
	return 0;
}













SpiDev_open(SpiDevObject *self, PyObject *args, PyObject *kwds)
{
	snprintf(path, SPIDEV_MAXPATH, "/dev/spidev%d.%d", bus, device)
	self->fd = open(path, O_RDWR, 0)
	ioctl(self->fd, SPI_IOC_RD_MODE, &tmp8)
	ioctl(self->fd, SPI_IOC_RD_MAX_SPEED_HZ, &tmp32)
}
