
class Vehicle {

public:
    double delta_s;
    double speed;
    int current_lane;

    bool operator < (const Vehicle &v)
    {
        return (delta_s < v.delta_s);
    }

    bool operator <= (const Vehicle &v)
    {
        return (delta_s <= v.delta_s);
    }

    bool operator > (const Vehicle &v)
    {
        return (delta_s > v.delta_s);
    }

    bool operator >= (const Vehicle &v)
    {
        return (delta_s >= v.delta_s);
    }
};
