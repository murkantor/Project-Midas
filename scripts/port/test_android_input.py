"""Test the real JNI touch-state publication code without SDL or game assets.

Virtual SDL attachment and physical controller behaviour still need device tests.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AndroidInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not compiler:
            raise unittest.SkipTest("A host C compiler is required")
        source = (ROOT / "port/android/android_input.c").read_text(encoding="utf-8")
        state = source[source.index("/* Keep in step"):source.index("/* Game-thread only. */")]
        reset = source[source.index("void portAndroidInputReset(void)"):
                       source.index("int portAndroidInputVirtualDeviceIndex(void)")]
        getters = source[source.index("int portAndroidInputAxisValue(int axis)"):
                         source.index("/* ---- raphnet HID backend")]
        jni = source[source.index("/* ---- JNI:"):]
        cls.tmp = tempfile.TemporaryDirectory(prefix="midas-input-")
        cls.addClassCleanup(cls.tmp.cleanup)
        directory = Path(cls.tmp.name)
        code = directory / "input.c"
        code.write_text(r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
typedef void JNIEnv;
typedef void *jclass;
typedef int jint;
typedef float jfloat;
typedef unsigned char jboolean;
#define JNIEXPORT
#define JNICALL
''' + state + reset + getters + jni + r'''
int main(void) {
    int i;
    portAndroidInputReset();
    for (i=0;i<4;i++) assert(portAndroidInputAxisValue(i)==0);
    for (i=4;i<6;i++) assert(portAndroidInputAxisValue(i)==-32768);
    Java_org_midas_android_AndroidInput_setAxisNative(NULL,NULL,0,2.0f);
    assert(portAndroidInputAxisValue(0)==32767);
    Java_org_midas_android_AndroidInput_setAxisNative(NULL,NULL,1,-2.0f);
    assert(portAndroidInputAxisValue(1)==-32767);
    Java_org_midas_android_AndroidInput_setAxisNative(NULL,NULL,4,1.0f);
    assert(portAndroidInputAxisValue(4)==32767);
    Java_org_midas_android_AndroidInput_setAxisNative(NULL,NULL,5,-1.0f);
    assert(portAndroidInputAxisValue(5)==-32768);
    Java_org_midas_android_AndroidInput_setAxisNative(NULL,NULL,0,NAN);
    assert(portAndroidInputAxisValue(0)==0);
    Java_org_midas_android_AndroidInput_setAxisNative(NULL,NULL,100,1.0f);
    Java_org_midas_android_AndroidInput_setButtonNative(NULL,NULL,0,1);
    Java_org_midas_android_AndroidInput_setButtonNative(NULL,NULL,14,1);
    assert(portAndroidInputButtonMask()==((1u<<14)|1));
    Java_org_midas_android_AndroidInput_setButtonNative(NULL,NULL,0,0);
    assert(portAndroidInputButtonMask()==(1u<<14));
    Java_org_midas_android_AndroidInput_setButtonNative(NULL,NULL,99,1);
    assert(portAndroidInputButtonMask()==(1u<<14));
    Java_org_midas_android_AndroidInput_setTouchActiveNative(NULL,NULL,1);
    assert(__atomic_load_n(&s_WantActive,__ATOMIC_ACQUIRE)==1);
    Java_org_midas_android_AndroidInput_setTouchActiveNative(NULL,NULL,0);
    assert(!portAndroidInputButtonMask());
    for(i=0;i<4;i++) assert(!portAndroidInputAxisValue(i));
    assert(portAndroidInputAxisValue(4)==-32768);
    assert(!__atomic_load_n(&s_WantActive,__ATOMIC_ACQUIRE));
    return 0;
}
''', encoding="utf-8")
        cls.binary = directory / ("input.exe" if os.name == "nt" else "input")
        subprocess.run([compiler, "-std=c11", str(code), "-o", str(cls.binary)], check=True)

    def test_axes_buttons_invalid_values_and_hide_reset(self):
        subprocess.run([str(self.binary)], check=True)


if __name__ == "__main__":
    unittest.main()
