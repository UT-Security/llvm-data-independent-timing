int f1(int a1, int a2, int a3) {
  int a4 = a1 + a2;
  int a5;

  if (a4 > 0)
    a5 = a2;
  else
    a5 = a3;

  return a5;
}

int f2(int b1, int b2) {
  if (b1 > b2)
    return 0;

  return b1;
}

int main() {
  f1(1, 2, 3);
  f2(1, 2);
  return 0;
}
