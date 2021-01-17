#include <iostream>

using namespace std;

template<class T>
void f1(T *t)
{
    cout << "*t="<<"*t"<<endl;
}
template<class T>
void f1(T t)
{
    cout << "t=" << t<<endl;
}

int main()
{
    int i=3;
    f1(i);
}