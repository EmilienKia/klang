
fibo(i: unsigned short) : unsigned int {
    if(i==0) return 1;
    else if(i==1) return 1;
    return fibo(i-1) + fibo(i-2);
}
