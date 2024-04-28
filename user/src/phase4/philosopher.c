#include "philosopher.h"

#define PHI_NUM 5

int forks[PHI_NUM];
//int locks[PHI_NUM];

void init() {
  // 初始化筷子和锁
  for (int i = 0; i < PHI_NUM; ++i) {
    forks[i] = sem_open(1);
  }
}

void philosopher(int id) {
  while (1) {
    think(id);  // 哲学家开始思考
    // 获取左边的筷子
    if(id % 2 == 1){
      P(forks[id]);
      // printf(id + "拿到了左侧的筷子");
      P(forks[(id + 1) % PHI_NUM]);
      // printf(id + "拿到了右侧的筷子");
      eat(id);  // 哲学家就餐
      V(forks[(id + 1) % PHI_NUM]);
      V(forks[id]);
    }else{
      P(forks[(id + 1) % PHI_NUM]);
      // printf(id + "拿到了右侧的筷子");
      P(forks[id]);
      // printf(id + "拿到了左侧的筷子");
      eat(id);  // 哲学家就餐
      V(forks[id]);
      V(forks[(id + 1) % PHI_NUM]);
    }
  }
}