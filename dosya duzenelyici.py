import os
import shutil

# 1. Ana hedef klasörün (r harfini unutma!)
ana_hedef_klasor = r"C:\Users\PC\Downloads"

# Botun dokunmaması gereken (kendi oluşturduğu) klasör adları
muaf_klasorler = ["Resimler", "Belgeler", "Videolar", "Muzikler", "Arsivler", "Programar", "Diger"]

uzanti_haritasi = {
    ".jpg": "Resimler", ".jpeg": "Resimler", ".png": "Resimler",
    ".pdf": "Belgeler", ".docx": "Belgeler", ".txt": "Belgeler",
    ".mp4": "Videolar", ".mp3": "Muzikler",
    ".zip": "Arsivler", ".rar": "Arsivler",
    ".exe": "Programlar"
}

def duzenle():
    # os.walk sayesinde en alt klasöre kadar her yeri gezeriz
    # topdown=False yapıyoruz ki önce en içteki dosyaları taşıyalım
    for kok_dizin, alt_dizinler, dosyalar in os.walk(ana_hedef_klasor, topdown=False):
        
        # Eğer botun kendi oluşturduğu klasörün içindeysek, orayı kurcalama
        klasor_adi = os.path.basename(kok_dizin)
        if klasor_adi in muaf_klasorler:
            continue

        for dosya_adi in dosyalar:
            # Python dosyasının kendisini taşımasını engelleyelim
            if dosya_adi == "duzenleyici.py":
                continue

            dosya_yolu = os.path.join(kok_dizin, dosya_adi)
            uzanti = os.path.splitext(dosya_adi)[1].lower()

            # Kategori belirleme
            if uzanti in uzanti_haritasi:
                kategori = uzanti_haritasi[uzanti]
            else:
                kategori = "Diger"

            # Dosyayı her zaman ANA klasördeki ilgili yere taşıyoruz
            hedef_dizin = os.path.join(ana_hedef_klasor, kategori)

            if not os.path.exists(hedef_dizin):
                os.makedirs(hedef_dizin)

            # Dosyayı taşı (Eğer aynı isimde dosya varsa üzerine yazmaz, hata vermemesi için kontrol eklenebilir)
            try:
                shutil.move(dosya_yolu, os.path.join(hedef_dizin, dosya_adi))
                print(f"Taşındı: {dosya_adi} -> {kategori}")
            except Exception as e:
                print(f"Hata: {dosya_adi} taşınamadı. Sebep: {e}")

        # Opsiyonel: Dosyaları boşalan alt klasörleri silebilirsin
        # if not os.listdir(kok_dizin) and kok_dizin != ana_hedef_klasor:
        #    os.rmdir(kok_dizin)

if __name__ == "__main__":
    duzenle()
    print("\n[!] Tüm alt klasörler tarandı ve düzenleme tamamlandı!")
    
