#include <iostream>



void syszuxPrint(){
    std::cout<<"shit ";
    
}

template<typename T, typename... Ts>
void syszuxPrint(T arg1, Ts... arg_left){
    std::cout<<arg1<<", ";
    syszuxPrint(arg_left...);
}

int main(int argc, char** argv)
{
    syszuxPrint(719,7030,"civilnet");
}