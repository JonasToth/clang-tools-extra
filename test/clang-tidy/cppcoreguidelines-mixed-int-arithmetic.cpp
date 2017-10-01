// RUN: %check_clang_tidy %s cppcoreguidelines-mixed-int-arithmetic %t

void mixed() {
    unsigned int UInt1 = 42;
    signed int SInt1 = 42;

    auto R1 = UInt1 + SInt1;
    int R2 = UInt1 + SInt1;
    unsigned int R3 = UInt1 + SInt1;
}
