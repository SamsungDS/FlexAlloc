cdef class XnvmeDev:
    def __init__(self, dev_uri: str):
        self._dev_uri_py_str = dev_uri.encode("ascii")
        cdef char *c_str = self._dev_uri_py_str
        self.data = NULL
        if fla_xne_dev_open(c_str, NULL, &self.data):
            raise RuntimeError("failed to open device")
        self.owner = True

    def __repr__(self):
        if self.data != NULL:
            return "XnvmeDev<~dev~>"
        else:
            return "XnvmeDev<~null~>"

    def __str__(self):
        return self.__repr__()

    @staticmethod
    cdef public XnvmeDev from_ptr(xnvme_dev *fs):
        cdef XnvmeDev inst = XnvmeDev.__new__(XnvmeDev)
        inst.data = fs
        inst.owner = False
        return inst

    def close(self):
        if self.data == NULL:
            return  # already closed
        fla_xne_dev_close(self.data)
        self.data = NULL

    def __dealloc__(self):
        if self.owner and self.data != NULL:
            self.close()


# TODO: wrap more C functions from pxd file

def xne_dev_tbytes(dev: XnvmeDev) -> int:
    return fla_xne_dev_tbytes(dev.data)


def xne_dev_lba_nbytes(dev: XnvmeDev) -> int:
    return fla_xne_dev_lba_nbytes(dev.data)
