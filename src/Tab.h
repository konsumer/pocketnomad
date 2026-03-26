class Tab {
public:
  const char* name;
  Tab(const char* name) : name(name) {}

  virtual void setup()  {}
  virtual void update() {}
  virtual ~Tab() {}
};