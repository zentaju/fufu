import numpy as np
import matplotlib.pyplot as plt
from scipy.integrate import solve_ivp

# --- 1. SİSTEM VE MOTOR PARAMETRELERİ (11 kV, 50 Hz) ---
V_line_rms = 11000.0        
V_phase_peak = (V_line_rms * np.sqrt(2)) / np.sqrt(3) 
f = 50.0                    
omega_e = 2 * np.pi * f     

# 11 kV, ~1.2 MW Büyük OG Motoru d-q Parametreleri
Rs = 0.12      
Rr = 0.10      
Lls = 0.0015   
Llr = 0.0015   
Lm = 0.045     

Ls = Lls + Lm
Lr = Llr + Lm
sigma = Ls * Lr - Lm**2

P_poles = 4    
J = 40.0       
Tl = 5000.0    # Nominal Yük Torku (Nm)

# Kompanzasyon Kapasiteleri ve Limitleri
C_klasik = 15.8e-6  # Sabit kondansatör (~600 kVAR)
Q_tsc_max = 600000.0 # TSC'nin basabileceği MAKSİMUM dinamik güç (600 kVAR)
tsc_delay = 0.03    # Tristörlerin ölçüm ve ateşleme gecikmesi (30 milisaniye)

# --- 2. ASENKRON MOTOR DİNAMİK MODELİ ---
def motor_ode(t, y):
    ids, iqs, idr, iqr, omega_r = y
    v_ds = V_phase_peak * np.cos(omega_e * t)
    v_qs = -V_phase_peak * np.sin(omega_e * t)
    
    d_ids = (Lr*v_ds - Lr*Rs*ids + Lm*Rr*idr + Lm*Lr*omega_r*iqr + Lm**2*omega_r*iqs) / sigma
    d_iqs = (Lr*v_qs - Lr*Rs*iqs + Lm*Rr*iqr - Lm*Lr*omega_r*idr - Lm**2*omega_r*ids) / sigma
    d_idr = (-Lm*v_ds + Lm*Rs*ids - Ls*Rr*idr - Ls*Lr*omega_r*iqr - Ls*Lm*omega_r*iqs) / sigma
    d_iqr = (-Lm*v_qs + Lm*Rs*iqs - Ls*Rr*iqr + Ls*Lr*omega_r*idr + Ls*Lm*omega_r*ids) / sigma
    
    Te = 1.5 * (P_poles / 2) * Lm * (iqs * idr - ids * iqr)
    d_omega_r = (Te - Tl) / J
    return [d_ids, d_iqs, d_idr, d_iqr, d_omega_r]

# Modeli Koştur
t_span = (0.0, 2.0)
t_eval = np.linspace(0, 2.0, 4000)
y0 = [0.0, 0.0, 0.0, 0.0, 0.0]
sol = solve_ivp(motor_ode, t_span, y0, t_eval=t_eval, method='RK45')

t = sol.t
ids, iqs, idr, iqr, omega_r = sol.y

# Güç Hesapları
v_ds = V_phase_peak * np.cos(omega_e * t)
v_qs = -V_phase_peak * np.sin(omega_e * t)
P_motor = 1.5 * (v_ds * ids + v_qs * iqs)
Q_motor = 1.5 * (v_qs * ids - v_ds * iqs)

# --- 3. GERÇEKÇİ ŞEBEKE ETKİLERİ VE SENARYOLAR ---

# Senaryo 1: Kompanzasyonsuz
P_seb_1 = P_motor
Q_seb_1 = Q_motor
I_seb_1 = np.sqrt(P_seb_1**2 + Q_seb_1**2) / (V_line_rms * np.sqrt(3))

# Senaryo 2: Klasik Sabit Kompanzasyon (~600 kVAR sabit bağlı)
Q_c_sabit = 3 * (V_line_rms / np.sqrt(3))**2 * omega_e * C_klasik
Q_seb_2 = Q_motor - Q_c_sabit
P_seb_2 = P_motor
I_seb_2 = np.sqrt(P_seb_2**2 + Q_seb_2**2) / (V_line_rms * np.sqrt(3))

# Senaryo 3: Sadece TSC'den Oluşan Gerçekçi SVC
Q_seb_3 = np.zeros_like(Q_motor)
for i, time in enumerate(t):
    if time < tsc_delay:
        # İlk 30 ms tristörler henüz uyanamadı, TSC sıfır basıyor!
        Q_tsc = 0.0
    else:
        # Tristörler devrede ama kapasite limiti (600 kVAR) var!
        # Motorun o anki reaktif ihtiyacı neyse onu basmaya çalışır ama limitine takılır
        Q_tsc = np.minimum(Q_motor[i], Q_tsc_max)
        
    Q_seb_3[i] = Q_motor[i] - Q_tsc

P_seb_3 = P_motor
I_seb_3 = np.sqrt(P_seb_3**2 + Q_seb_3**2) / (V_line_rms * np.sqrt(3))

# Güç Faktörleri
cos_phi_1 = np.clip(np.abs(P_seb_1) / (np.sqrt(P_seb_1**2 + Q_seb_1**2) + 1e-6), 0, 1)
cos_phi_2 = np.clip(np.abs(P_seb_2) / (np.sqrt(P_seb_2**2 + Q_seb_2**2) + 1e-6), 0, 1)
cos_phi_3 = np.clip(np.abs(P_seb_3) / (np.sqrt(P_seb_3**2 + Q_seb_3**2) + 1e-6), 0, 1)

# --- 4. GRAFİKLERLE GÖSTERİM ---
plt.figure(figsize=(14, 11))

# Grafik 1: Şebeke Akımları (Neden kalkışta aynılar?)
plt.subplot(3, 1, 1)
plt.plot(t, I_seb_1, 'r-', label='Kompanzasyonsuz Sistem')
plt.plot(t, I_seb_2, 'b--', label='Klasik Sabit Kompanzasyon')
plt.plot(t, I_seb_3, 'g-', lw=2, label='Sadece TSC (Gerçekçi SVC)')
plt.title('11 kV / 50 Hz Şebekeden Çekilen Akım ve Güç Analizi (Gerçekçi Limitlerle)', fontsize=14)
plt.ylabel('Şebeke RMS Akımı (Amper)')
plt.grid(True)
plt.legend(loc='upper right')

# Grafik 2: Şebekeden Çekilen Net Reaktif Güç (Q)
plt.subplot(3, 1, 2)
plt.plot(t, Q_motor / 1e6, 'k:', lw=1.5, label='Motorun Gerçek Reaktif İhtiyacı')
plt.plot(t, Q_seb_1 / 1e6, 'r-', label='Kompanzasyonsuz Şebeke Yükü')
plt.plot(t, Q_seb_2 / 1e6, 'b--', label='Klasik Sabit Kompanzasyon Şebeke Yükü')
plt.plot(t, Q_seb_3 / 1e6, 'g-', lw=2, label='Sadece TSC Şebeke Yükü')
plt.ylabel('Şebekeden Çekilen Reaktif (MVAR)')
plt.grid(True)
plt.legend(loc='upper right')

# Grafik 3: Şebeke Güç Faktörleri
plt.subplot(3, 1, 3)
plt.plot(t, cos_phi_1, 'r-', label='Kompanzasyonsuz $\cos\phi$')
plt.plot(t, cos_phi_2, 'b--', label='Klasik Kompanzasyon $\cos\phi$')
plt.plot(t, cos_phi_3, 'g-', lw=2, label='Sadece TSC $\cos\phi$')
plt.xlabel('Zaman (Saniye)', fontsize=12)
plt.ylabel('Güç Faktörü ($\cos\phi$)')
plt.ylim(0, 1.05)
plt.grid(True)
plt.legend(loc='lower right')

plt.tight_layout()
plt.show()
