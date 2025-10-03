\
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

// Simple LP writer for differential MILP skeleton:
// Variables: dA_r_i, dB_r_i, carry_k_r_i for four additions per round.
// Constraints: sum bit parity and carry recursion linearization.
// Objective: minimize total carries (LM weight proxy).

static void write_lp(const std::string& path, int R, int n){
    std::ofstream lp(path);
    if (!lp){ std::fprintf(stderr, "Cannot open %s\n", path.c_str()); return; }

    lp << "\\* NeoAlzette differential MILP (skeleton) *\\\n";
    lp << "Minimize\n obj: ";

    // objective: sum of carries
    bool first=true;
    for (int r=0;r<R;r++){
        for (int k=0;k<4;k++){
            for (int i=0;i<n;i++){
                if (!first) lp << " + ";
                lp << "c_"<<k<<"_"<<r<<"_"<<i;
                first=false;
            }
        }
    }
    lp << "\nSubject To\n";

    // constraints per round/addition/bit
    // For simplicity, we only write a generic full-adder relation:
    // z_i = x_i + y_i + c_i (mod 2)  -> linearized via auxiliary u vars: z_i - x_i - y_i - c_i + 2*u_i = 0
    // c_{i+1} >= x_i + y_i + c_i - 1
    // and c_{i+1} <= x_i + y_i + c_i (three inequalities can be added for tightness).
    auto var = [](const char* p, int r, int i){
        std::ostringstream ss; ss<<p<<"_"<<r<<"_"<<i; return ss.str();
    };

    for (int r=0;r<R;r++){
        for (int add=0; add<4; ++add){
            for (int i=0;i<n;i++){
                // x_i, y_i, z_i, c_i, c_{i+1}
                std::string x = "x_"+std::to_string(add)+"_"+std::to_string(r)+"_"+std::to_string(i);
                std::string y = "y_"+std::to_string(add)+"_"+std::to_string(r)+"_"+std::to_string(i);
                std::string z = "z_"+std::to_string(add)+"_"+std::to_string(r)+"_"+std::to_string(i);
                std::string c = "c_"+std::to_string(add)+"_"+std::to_string(r)+"_"+std::to_string(i);
                std::string cn= "c_"+std::to_string(add)+"_"+std::to_string(r)+"_"+std::to_string((i+1)%n);
                std::string u = "u_"+std::to_string(add)+"_"+std::to_string(r)+"_"+std::to_string(i);

                lp << " eq_parity_"<<add<<"_"<<r<<"_"<<i<<": "<< z <<" - "<< x <<" - "<< y <<" - "<< c <<" + 2 "<< u <<" = 0\n";
                lp << " ineq_c_ge_"<<add<<"_"<<r<<"_"<<i<<": "<< cn <<" - "<< x <<" - "<< y <<" - "<< c <<" >= -1\n";
                lp << " ineq_c_le1_"<<add<<"_"<<r<<"_"<<i<<": "<< cn <<" - "<< x <<" <= 0\n";
                lp << " ineq_c_le2_"<<add<<"_"<<r<<"_"<<i<<": "<< cn <<" - "<< y <<" <= 0\n";
                lp << " ineq_c_le3_"<<add<<"_"<<r<<"_"<<i<<": "<< cn <<" - "<< c <<" <= 0\n";
            }
        }
    }

    // variable bounds (binary)
    lp << "Bounds\n";
    lp << "Binary\n";
    for (int r=0;r<R;r++){
        for (int add=0; add<4; ++add){
            for (int i=0;i<n;i++){
                lp << " x_"<<add<<"_"<<r<<"_"<<i<<"\n";
                lp << " y_"<<add<<"_"<<r<<"_"<<i<<"\n";
                lp << " z_"<<add<<"_"<<r<<"_"<<i<<"\n";
                lp << " c_"<<add<<"_"<<r<<"_"<<i<<"\n";
            }
        }
    }
    // u variables are integer
    lp << "Generals\n";
    for (int r=0;r<R;r++){
        for (int add=0; add<4; ++add){
            for (int i=0;i<n;i++){
                lp << " u_"<<add<<"_"<<r<<"_"<<i<<"\n";
            }
        }
    }
    lp << "End\n";
    lp.close();
    std::printf("Wrote differential MILP LP to %s\n", path.c_str());
}

int main(int argc, char** argv){
    if (argc < 4){
        std::fprintf(stderr, "Usage: %s R n out.lp\n", argv[0]);
        return 1;
    }
    int R = std::stoi(argv[1]);
    int n = std::stoi(argv[2]);
    std::string out = argv[3];
    write_lp(out, R, n);
    return 0;
}
