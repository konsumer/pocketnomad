// Like int, but rolls around on modulo

class Counter {
public:
  int val;
  int size;
  Counter(int size = 1, int val = 0) : size(size), val(val) {}
  int increment(int n = 1) { return val = ((val + n) % size + size) % size; }
  int decrement(int n = 1) { return val = ((val - n) % size + size) % size; }

  operator int() const { return val; }

  Counter& operator++()    { val = ((val + 1) % size + size) % size; return *this; }
  Counter& operator--()    { val = ((val - 1) % size + size) % size; return *this; }
  Counter  operator++(int) { Counter tmp = *this; ++(*this); return tmp; }
  Counter  operator--(int) { Counter tmp = *this; --(*this); return tmp; }
  Counter& operator+=(int n) { val = ((val + n) % size + size) % size; return *this; }
  Counter& operator-=(int n) { val = ((val - n) % size + size) % size; return *this; }
};