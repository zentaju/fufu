import requests
import webbrowser

# API KEY'ini buraya yaz
API_KEY = "xxxxxx"

def detayli_film_botu():
    print("🎬 İngilizce Romantik Film Botu | Yeah!")
    print("-------------------------------------------")

    # 1. Kriterleri Kullanıcıdan Alalım
    # Sadece romantik filmler arayacağımız için kullanıcıdan sadece anahtar kelime alıyoruz
    anahtar_kelime = input("Aramak için bir kelime gir (Örn: Love, Wedding, Paris): ")
    try:
        baslangic_yili = int(input("Başlangıç yılı (Örn: 2015): "))
        bitis_yili = int(input("Bitiş yılı (Örn: 2023): "))
        min_puan = float(input("Minimum IMDb puanı (Örn: 7.0): "))
    except ValueError:
        print("Lütfen sayısal değerleri doğru girin!")
        return

    params = {
        "apikey": API_KEY,
        "s": anahtar_kelime,
        "type": "movie"
    }

    url = "http://www.omdbapi.com/"
    
    try:
        print("\n🔍 Kriterlerine uygun romantik filmler taranıyor, lütfen bekle...")
        response = requests.get(url, params=params)
        data = response.json()

        if data.get("Response") == "True":
            ham_filmler = data["Search"]
            uygun_filmler = []

            for f in ham_filmler:
                # Her filmin detayına gidip tür, dil, yıl ve puan kontrolü yapıyoruz
                detay = requests.get(url, params={"apikey": API_KEY, "i": f["imdbID"]}).json()
                
                # Tür (Genre) ve Dil (Language) bilgilerini alalım
                film_turu = detay.get("Genre", "")
                film_dili = detay.get("Language", "")

                # 🌟 ŞART 1: Tür içinde "Romance" ve Dil içinde "English" geçmiyorsa bu filmi atla
                if "Romance" not in film_turu or "English" not in film_dili:
                    continue

                # Yıl bilgisini temizleyelim
                try:
                    film_yili = int(str(detay.get("Year"))[:4])
                    imdb_puani_str = detay.get("imdbRating", "0")
                    imdb_puani = float(imdb_puani_str if imdb_puani_str != "N/A" else 0)
                except:
                    continue

                # 🌟 ŞART 2: Senin yıl ve puan kriterlerine uyuyor mu?
                if baslangic_yili <= film_yili <= bitis_yili and imdb_puani >= min_puan:
                    uygun_filmler.append(detay)

            # Sonuçları Göster
            if uygun_filmler:
                print(f"\n✅ Kriterlerine uyan {len(uygun_filmler)} İngilizce romantik film bulundu:\n")
                for i, film in enumerate(uygun_filmler, 1):
                    print(f"{i}- {film['Title']} ({film['Year']})")
                    print(f"   ⭐ Puan: {film['imdbRating']} | 🎭 Tür: {film['Genre']} | 🗣️ Dil: {film['Language']}")
                    print(f"   📜 Özet: {film['Plot'][:120]}...")
                    print("-" * 50)

                secim = input("\nDetaylı incelemek için film numarası seç (Çıkmak için Enter): ")
                if secim.isdigit() and 1 <= int(secim) <= len(uygun_filmler):
                    webbrowser.open(f"https://www.imdb.com/title/{uygun_filmler[int(secim)-1]['imdbID']}/")
            else:
                print("\n😔 Maalesef belirttiğin kelime, yıl ve puana uygun İngilizce romantik bir film bulamadım.")
        else:
            print(f"\n❌ Hata: {data.get('Error')}")

    except Exception as e:
        print(f"Bir hata oluştu: {e}")

if __name__ == "__main__":
    detayli_film_botu()
