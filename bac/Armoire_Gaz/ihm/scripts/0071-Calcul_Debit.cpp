// Debit massique (kg/h) a partir du debit volumique (m3/h)
#include <cmath>

double calculDebit(double m3h, double densite)
{
    if (m3h < 0.0) {
        return 0.0;
    }
    return std::round(m3h * densite * 10.0) / 10.0;
}
