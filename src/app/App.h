#ifndef APP_H
#define APP_H

class App {
public:
    static App& getInstance();

    void init();
    void loop();

private:
    App() = default;
    void markImageValidWhenStable();

    unsigned long lastBroadcast_     = 0;
    unsigned long lastMeasurement_   = 0;
    unsigned long lastHistoryCheck_  = 0;
    bool imageValidated_ = false;
};

#endif // APP_H
