#include<bits/stdc++.h>
using namespace std;
void f(int i,int arr[],int n)
{
    if(n>=n/2)return;

    swap(arr[i],arr[n-i-1]);
    f(i+1,arr,n);
}
int main()
{
     int n;
     cout<<"Enter the size of the array"<<endl;
     cin>>n;
     int arr[n];
     for(int i=0;i<n;i++)
     {
        cin>> arr[i];
        for(int i=0;i<n;++i)
        f(0,arr,n);

     }
     for(int i=0;i<n;++i)cout<<arr[i]<<endl;

}